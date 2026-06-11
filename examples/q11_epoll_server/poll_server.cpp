// Вопрос 11: echo-сервер на poll(2) — «select без лимита FD_SETSIZE».
//
// poll() лечит главную боль select — фиксированную битовую маску. Вместо
// fd_set мы передаём ЯВНЫЙ массив структур pollfd {fd, events, revents}:
//
//   - НЕТ лимита FD_SETSIZE: следим за любым числом дескрипторов с любыми
//     номерами; потолок — только RLIMIT_NOFILE (лимит открытых файлов).
//   - Массив pollfd НЕ разрушается: запрашиваемые события лежат в .events,
//     а ядро пишет результат в ОТДЕЛЬНОЕ поле .revents. Поэтому полную
//     пересборку (как FD_ZERO/FD_SET у select) делать НЕ нужно — массив
//     переиспользуется между вызовами.
//   - pollfd.fd = -1 ИГНОРИРУЕТСЯ ядром — удобный способ «выключить» слот,
//     не сдвигая массив (мы этим пользуемся при пометке закрытых клиентов).
//
// Чего poll НЕ исправил (и почему дальше пришёл epoll):
//   - всё ещё O(N): на КАЖДЫЙ вызов ядро КОПИРУЕТ весь массив pollfd
//     из user space и ЛИНЕЙНО сканирует его целиком, даже если активен
//     один сокет; и мы сами после возврата делаем O(N) проход по .revents.
//   epoll же регистрирует интерес ОДИН раз и возвращает только готовые fd.
//
// Семантика LEVEL-TRIGGERED (как у select): читаем по одному буферу за
// готовность; пока есть данные, poll снова отметит POLLIN.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q11_epoll_server/poll_server.cpp -o bin/q11_poll
// run:     ./bin/q11_poll     (nc 127.0.0.1 8080)

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
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 8080;
constexpr int kBacklog = 128;
constexpr std::size_t kBufferSize = 4096;

// RAII-обёртка над файловым дескриптором: close() в деструкторе.
// release() отдаёт владение наружу (нужно, когда fd начинает жить в массиве
// pollfd и закрывается вручную).
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
// достаточно — остаток данных даст POLLIN на следующей итерации.
// Возвращает true, если соединение надо закрыть.
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

// Принять одно ожидающее соединение и добавить новый pollfd с POLLIN.
// fd живёт теперь в массиве fds, поэтому release().
void accept_client(int listen_fd, std::vector<pollfd>& fds) {
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
    pollfd entry{};
    entry.fd = client.release();  // владение переходит в массив
    entry.events = POLLIN;
    entry.revents = 0;
    fds.push_back(entry);
}

// O(N) проход по массиву pollfd после возврата poll. Индекс 0 — listen.
// Закрытые клиенты помечаем fd = -1, чтобы не двигать массив прямо в цикле
// (poll такие слоты игнорирует), компактим отдельным проходом.
void service_pollfds(std::vector<pollfd>& fds, int listen_fd) {
    for (std::size_t index = 0; index < fds.size(); ++index) {
        const short revents = fds[index].revents;
        if (revents == 0) {
            continue;  // этот дескриптор не готов
        }
        if (fds[index].fd == listen_fd) {
            accept_client(listen_fd, fds);  // может добавить элементы в конец
            continue;
        }
        // POLLHUP/POLLERR/POLLNVAL = клиент отвалился или ошибка.
        const bool failed = (revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
        if (failed || (((revents & POLLIN) != 0) && read_once(fds[index].fd))) {
            ::close(fds[index].fd);
            fds[index].fd = -1;  // помечаем слот на удаление
        }
    }
}

// Убрать помеченные (fd == -1) слоты swap-and-pop. Listen (индекс 0) никогда
// не помечается, поэтому остаётся на месте.
void compact_pollfds(std::vector<pollfd>& fds) {
    std::size_t index = 0;
    while (index < fds.size()) {
        if (fds[index].fd < 0) {
            fds[index] = fds.back();
            fds.pop_back();
            continue;  // на место встал последний — не инкрементируем
        }
        ++index;
    }
}

// Главный цикл на poll: массив pollfd переиспользуется между вызовами,
// ядро само сбрасывает .revents. Спим в poll, обрабатываем, компактим.
void run_poll_loop() {
    const FileDescriptor listener = make_listener(kPort);

    // Индекс 0 — listen-сокет с POLLIN (готов = есть кого accept).
    std::vector<pollfd> fds;
    pollfd listen_entry{};
    listen_entry.fd = listener.get();
    listen_entry.events = POLLIN;
    listen_entry.revents = 0;
    fds.push_back(listen_entry);

    std::cout << "q11: poll сервер слушает порт " << kPort << '\n';

    for (;;) {
        // -1 = ждать бесконечно. Массив НЕ пересобираем — лишь передаём его.
        const int ready = ::poll(fds.data(), fds.size(), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;  // прервано сигналом
            }
            std::perror("poll");
            break;
        }
        service_pollfds(fds, listener.get());
        compact_pollfds(fds);
    }

    // listener закроется RAII; клиенты обычно закрыты вручную, но оставшиеся
    // (если выходим по ошибке) закроем здесь, кроме самого listen.
    for (const pollfd& entry : fds) {
        if (entry.fd >= 0 && entry.fd != listener.get()) {
            ::close(entry.fd);
        }
    }
}

}  // namespace

// Сравнение трёх API мультиплексирования (вопрос 11):
//   select : лимит FD_SETSIZE=1024, пересборка fd_set каждый раз, O(N) скан в ядре
//   poll   : без лимита, массив pollfd переиспользуется, всё ещё O(N) скан
//   epoll  : O(1) на готовый fd, ядро ведёт ready-list, масштаб до 100k+ соединений
int main() {
    try {
        run_poll_loop();
    } catch (const std::system_error& error) {
        std::cerr << "фатальная ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
