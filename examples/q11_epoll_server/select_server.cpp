// Вопрос 11: echo-сервер на select(2) — самый старый из трёх API.
//
// select() решает ту же задачу, что epoll, но «в лоб»: даём ядру битовую
// маску дескрипторов, ядро линейно сканирует её и отмечает готовые. Отсюда
// его врождённые ограничения, которые и привели к появлению poll, а затем
// epoll:
//
//   1) FD_SETSIZE = 1024 — это РАЗМЕР битовой маски fd_set. Нельзя следить
//      за дескриптором с НОМЕРОМ >= 1024 (не «за 1024 штуками», а именно за
//      номером — даже если открыт всего один fd, но с номером 2000, UB).
//   2) fd_set РАЗРУШАЕТСЯ каждым select(): на входе это «за чем следить»,
//      на выходе — «что готово». Поэтому маску надо ПЕРЕСОБИРАТЬ заново
//      (FD_ZERO + FD_SET по всем fd) на КАЖДОЙ итерации — это O(N).
//   3) Ядро линейно сканирует все биты от 0 до max_fd -> стоимость O(max_fd)
//      на каждый вызов, независимо от числа реально активных соединений.
//   4) Аргумент timeout на Linux МОДИФИЦИРУЕТСЯ (в него пишется остаток
//      времени) -> при повторном вызове его нужно переустанавливать.
//
// Здесь LEVEL-TRIGGERED семантика (она у select единственно возможная):
// читаем по одному буферу за готовность; пока в сокете есть данные, select
// будет снова отмечать его готовым. EOF/ошибка -> закрыть и убрать из списка.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q11_epoll_server/select_server.cpp -o bin/q11_select
// run:     ./bin/q11_select     (nc 127.0.0.1 8080)

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
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 8080;
constexpr int kBacklog = 128;
constexpr std::size_t kBufferSize = 4096;

// RAII-обёртка над файловым дескриптором: close() в деструкторе.
// release() отдаёт владение наружу (нужно, когда fd начинает жить в нашем
// списке клиентов и закрывается вручную).
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

    // Отдать владение дескриптором наружу, не закрывая его.
    [[nodiscard]] int release() noexcept {
        return std::exchange(fd_, -1);
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

// Записать ровно count байт (partial-write цикл).
// Возвращает false при ошибке (соединение закрываем).
bool write_all(int client_fd, const char* data, std::size_t count) {
    std::size_t written = 0;
    while (written < count) {
        const ssize_t chunk = ::write(client_fd, data + written, count - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::cerr << "send buffer full, drop\n";
            } else {
                std::perror("write");
            }
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    return true;
}

// LT-чтение: читаем ОДИН буфер и эхо-отвечаем. При level-triggered этого
// достаточно — если данные остались, select снова отметит сокет готовым на
// следующей итерации. Возвращает true, если соединение надо закрыть.
bool read_once(int client_fd) {
    std::array<char, kBufferSize> buffer{};
    const ssize_t bytes_read = ::read(client_fd, buffer.data(), buffer.size());
    if (bytes_read > 0) {
        return !write_all(client_fd, buffer.data(),
                          static_cast<std::size_t>(bytes_read));
    }
    if (bytes_read == 0) {
        return true;  // EOF — клиент закрыл соединение
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return false;  // ложная готовность/прерывание — оставляем жить
    }
    std::perror("read");
    return true;
}

// Принять одно ожидающее соединение и добавить его в список клиентов.
// Listen-сокет неблокирующий, поэтому accept не подвиснет.
void accept_client(int listen_fd, std::vector<int>& clients) {
    FileDescriptor client{::accept(listen_fd, nullptr, nullptr)};
    if (!client.valid()) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            std::perror("accept");
        }
        return;
    }
    if (!set_nonblocking(client.get())) {
        return;  // client закроется в деструкторе
    }
    // Жёсткое ограничение select: за номером fd >= FD_SETSIZE следить нельзя.
    if (client.get() >= FD_SETSIZE) {
        std::cerr << "fd " << client.get()
                  << " >= FD_SETSIZE, select не потянет, drop\n";
        return;  // client закроется в деструкторе
    }
    clients.push_back(client.release());  // владение переходит в список
}

// Пересобрать read_set с нуля: listen + все клиенты. Это и есть «боль»
// select — набор разрушается каждым вызовом, поэтому строим заново КАЖДЫЙ
// раз. Возвращает max_fd (select требует nfds = max_fd + 1).
int rebuild_read_set(fd_set& read_set, int listen_fd,
                     const std::vector<int>& clients) {
    FD_ZERO(&read_set);
    FD_SET(listen_fd, &read_set);
    int max_fd = listen_fd;
    for (const int client_fd : clients) {
        FD_SET(client_fd, &read_set);
        if (client_fd > max_fd) {
            max_fd = client_fd;
        }
    }
    return max_fd;
}

// O(N) проход по клиентам после select: эхо для готовых, удаление закрытых.
// Закрытые удаляем swap-and-pop (порядок не важен для echo-сервера).
void service_clients(fd_set& read_set, std::vector<int>& clients) {
    std::size_t index = 0;
    while (index < clients.size()) {
        const int client_fd = clients[index];
        if (FD_ISSET(client_fd, &read_set) && read_once(client_fd)) {
            ::close(client_fd);
            clients[index] = clients.back();
            clients.pop_back();
            continue;  // на это место встал последний — не инкрементируем
        }
        ++index;
    }
}

// Главный цикл на select: на каждой итерации пересобираем fd_set, спим в
// select, затем O(N) обходим listen и всех клиентов.
void run_select_loop() {
    const FileDescriptor listener = make_listener(kPort);
    std::vector<int> clients;

    std::cout << "q11: select сервер слушает порт " << kPort << '\n';

    for (;;) {
        // fd_set разрушается каждым select -> строим заново (O(N)).
        fd_set read_set;
        const int max_fd = rebuild_read_set(read_set, listener.get(), clients);

        // timeout = nullptr -> ждать бесконечно. (Если бы передавали timeval,
        // его пришлось бы переустанавливать: Linux пишет в него остаток.)
        const int ready = ::select(max_fd + 1, &read_set, nullptr, nullptr, nullptr);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;  // прервано сигналом
            }
            std::perror("select");
            break;
        }

        // Новые соединения: listen-сокет готов на чтение = есть кого accept.
        if (FD_ISSET(listener.get(), &read_set)) {
            accept_client(listener.get(), clients);
        }
        service_clients(read_set, clients);
    }

    for (const int client_fd : clients) {
        ::close(client_fd);
    }
}

}  // namespace

// Сравнение трёх API мультиплексирования (вопрос 11):
//   select : лимит FD_SETSIZE=1024, пересборка fd_set каждый раз, O(N) скан в ядре
//   poll   : без лимита, массив pollfd переиспользуется, всё ещё O(N) скан
//   epoll  : O(1) на готовый fd, ядро ведёт ready-list, масштаб до 100k+ соединений
int main() {
    try {
        run_select_loop();
    } catch (const std::system_error& error) {
        std::cerr << "фатальная ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
