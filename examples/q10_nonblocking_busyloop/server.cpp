// Вопрос 10: неблокирующий echo-сервер с busy-loop polling (ОДИН поток).
//
// Идея: переводим все сокеты в режим O_NONBLOCK. Тогда accept()/read() не
// блокируют поток — если данных/соединений нет, возвращают -1 с errno=EAGAIN
// (он же EWOULDBLOCK). Поэтому один поток может "вертеть" сразу N клиентов:
// в бесконечном цикле он по очереди опрашивает (sweep) listen-сокет и всех
// клиентов из std::vector<FileDescriptor>.
//
// ЭТО НАМЕРЕННО ПЛОХОЙ ПОДХОД — он показывает проблемы, ради которых придумали
// epoll/io_uring:
//   [ПРОБЛЕМА 1] 100% CPU: даже когда никто ничего не шлёт, поток крутит
//                цикл вхолостую, постоянно дёргая syscall'ы и сжигая ядро.
//   [ПРОБЛЕМА 2] O(N) sweep на КАЖДОЙ итерации: проверяем всех клиентов, даже
//                если активен один. При 10k соединений 9999 проверок впустую
//                -> не масштабируется.
// Решение этих проблем — спросить ЯДРО "какие fd готовы?" одним вызовом:
// это epoll (q11) и io_uring (q12). Место, куда встал бы epoll_wait(),
// помечено ниже комментарием [СЮДА EPOLL].
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q10_nonblocking_busyloop/server.cpp -o bin/q10_server
// run:     ./bin/q10_server     (наблюдай 100% CPU в top даже без клиентов!)

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <system_error>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 8080;
constexpr int kBacklog = 128;
constexpr std::size_t kMaxClients = 1024;
constexpr std::size_t kBufferSize = 4096;

// RAII-обёртка над файловым дескриптором: close() в деструкторе.
class FileDescriptor {
public:
    FileDescriptor() = default;

    explicit FileDescriptor(int fd) noexcept
        : fd_{fd} {
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : fd_{std::exchange(other.fd_, -1)} {
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~FileDescriptor() {
        reset();
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }

private:
    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error{errno, std::generic_category(), what};
}

// Перевести дескриптор в неблокирующий режим.
// Сначала читаем текущие флаги (F_GETFL), затем добавляем O_NONBLOCK —
// нельзя просто F_SETFL O_NONBLOCK, иначе сотрём остальные флаги.
bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::perror("fcntl(F_GETFL)");
        return false;
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::perror("fcntl(F_SETFL O_NONBLOCK)");
        return false;
    }
    return true;
}

// Создать listen-сокет в неблокирующем режиме.
FileDescriptor make_listener(std::uint16_t port) {
    FileDescriptor listener{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!listener.valid()) {
        throw_errno("socket");
    }

    const int option = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                     &option, sizeof(option)) < 0) {
        throw_errno("setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0) {
        throw_errno("bind");
    }
    if (::listen(listener.get(), kBacklog) < 0) {
        throw_errno("listen");
    }
    if (!set_nonblocking(listener.get())) {
        throw std::system_error{errno, std::generic_category(),
                                "set_nonblocking(listen)"};
    }
    return listener;
}

// Принять все ожидающие соединения, пока accept не вернёт EAGAIN, и добавить
// их в список клиентов (с соблюдением лимита kMaxClients).
void try_accept(int listen_fd, std::vector<FileDescriptor>& clients) {
    for (;;) {
        FileDescriptor client{::accept(listen_fd, nullptr, nullptr)};
        if (!client.valid()) {
            // Нет ожидающих соединений — нормально для неблокирующего сокета.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            std::perror("accept");
            break;
        }
        if (!set_nonblocking(client.get())) {
            continue;  // client закроется в деструкторе
        }
        if (clients.size() >= kMaxClients) {
            std::cerr << "достигнут лимит клиентов, отклоняю\n";
            continue;  // client закроется в деструкторе
        }
        clients.push_back(std::move(client));
    }
}

// Результат одной попытки обслужить клиента.
enum class ClientState {
    kAlive,   // клиент жив, оставляем в списке
    kClosed,  // клиента надо удалить (EOF или ошибка)
};

// Записать ровно count байт в неблокирующий сокет (partial-write цикл).
// Возвращает false при ошибке записи (соединение надо закрыть).
bool write_all_nonblocking(int client_fd, const char* data, std::size_t count) {
    std::size_t written = 0;
    while (written < count) {
        const ssize_t chunk = ::write(client_fd, data + written, count - written);
        if (chunk < 0) {
            // EAGAIN: send-буфер полон; EINTR: прервано сигналом — повторяем.
            // (Для простоты примера крутим до конца, а не буферизуем остаток.)
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            std::perror("write");
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    return true;
}

// Один опрос (read + echo) одного клиента.
ClientState try_echo(int client_fd) {
    std::array<char, kBufferSize> buffer{};
    const ssize_t bytes_read = ::read(client_fd, buffer.data(), buffer.size());
    if (bytes_read > 0) {
        if (!write_all_nonblocking(client_fd, buffer.data(),
                                   static_cast<std::size_t>(bytes_read))) {
            return ClientState::kClosed;
        }
        return ClientState::kAlive;
    }
    if (bytes_read == 0) {
        return ClientState::kClosed;  // EOF — клиент закрыл соединение
    }
    // bytes_read < 0: EAGAIN — данных пока нет, это НЕ ошибка.
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return ClientState::kAlive;
    }
    std::perror("read");
    return ClientState::kClosed;
}

// O(N) sweep по всем клиентам: опрашиваем каждого, удаляем закрывшихся.
void sweep_clients(std::vector<FileDescriptor>& clients) {
    for (std::size_t i = 0; i < clients.size();) {
        if (try_echo(clients[i].get()) == ClientState::kClosed) {
            // swap-and-pop: O(1) удаление без сдвига хвоста.
            clients[i] = std::move(clients.back());
            clients.pop_back();
        } else {
            ++i;
        }
    }
}

// Бесконечный busy-loop: на каждой итерации полный проход по дескрипторам.
void run_busy_loop() {
    const FileDescriptor listener = make_listener(kPort);
    std::vector<FileDescriptor> clients;
    clients.reserve(kMaxClients);

    std::cout << "q10: неблокирующий busy-loop сервер на порту " << kPort
              << " (внимание: 100% CPU)\n";

    for (;;) {
        // [СЮДА EPOLL] В нормальном сервере здесь стоял бы epoll_wait(),
        // который УСНУЛ бы до появления событий и вернул только готовые fd.
        // Здесь же мы наугад опрашиваем всех -> вот и busy-loop.
        try_accept(listener.get(), clients);
        sweep_clients(clients);
        // Конец итерации -> сразу новая. Поток никогда не спит -> жжём CPU.
    }
}

}  // namespace

int main() {
    try {
        run_busy_loop();
    } catch (const std::system_error& error) {
        std::cerr << "фатальная ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
