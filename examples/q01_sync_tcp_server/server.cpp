// Вопрос 1: синхронный однопоточный (blocking) TCP echo-сервер.
//
// Демонстрирует классическую схему socket()->bind()->listen()->accept().
// Главная мысль вопроса: пока обслуживается ОДИН клиент (мы сидим в read/write),
// все остальные подключения стоят в очереди backlog и НЕ обслуживаются.
// Один поток = один клиент в каждый момент времени. Это мотивация для
// многопоточности (q02), неблокирующего I/O (q10) и epoll (q11).
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q01_sync_tcp_server/server.cpp -o bin/q01_server
// run:     ./bin/q01_server        (затем в другом терминале: nc 127.0.0.1 8080)

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <system_error>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 8080;
constexpr int kBacklog = 16;  // длина очереди ожидающих accept() соединений
constexpr std::size_t kBufferSize = 4096;

// RAII-обёртка над файловым дескриптором: close() в деструкторе.
// Запрещаем копирование, разрешаем перемещение (владение единственное).
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

// Бросает std::system_error с текущим errno и контекстным сообщением.
[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error{errno, std::generic_category(), what};
}

// Создать, привязать и перевести в режим прослушивания TCP-сокет на порту.
FileDescriptor make_listener(std::uint16_t port) {
    FileDescriptor listener{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!listener.valid()) {
        throw_errno("socket");
    }

    // SO_REUSEADDR: позволяет немедленно переиспользовать адрес/порт после
    // рестарта сервера. Без него bind() даёт EADDRINUSE, пока старое
    // соединение висит в состоянии TIME_WAIT (до 2*MSL, ~минуты).
    const int option = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                     &option, sizeof(option)) < 0) {
        throw_errno("setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);  // слушаем на всех интерфейсах
    address.sin_port = htons(port);               // порт в сетевом порядке байт

    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0) {
        throw_errno("bind");
    }
    if (::listen(listener.get(), kBacklog) < 0) {
        throw_errno("listen");
    }
    return listener;
}

// Записать ровно count байт, обрабатывая частичную запись и EINTR.
// Возвращает false, если соединение оборвалось при записи.
bool write_all(int client_fd, const char* data, std::size_t count) {
    std::size_t written = 0;
    while (written < count) {
        const ssize_t chunk = ::write(client_fd, data + written, count - written);
        if (chunk < 0) {
            // EINTR — системный вызов прерван сигналом, просто повторяем.
            if (errno == EINTR) {
                continue;
            }
            std::perror("write");
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    return true;
}

// Обрабатываем одного клиента целиком (echo) и возвращаемся.
// На время этой функции сервер "занят" — новые клиенты ждут в backlog.
void echo_loop(int client_fd) {
    std::array<char, kBufferSize> buffer{};
    // read() возвращает 0 при закрытии соединения клиентом (EOF).
    for (;;) {
        const ssize_t bytes_read = ::read(client_fd, buffer.data(), buffer.size());
        if (bytes_read > 0) {
            if (!write_all(client_fd, buffer.data(),
                           static_cast<std::size_t>(bytes_read))) {
                return;
            }
        } else if (bytes_read == 0) {
            return;  // нормальное закрытие со стороны клиента
        } else {
            if (errno == EINTR) {
                continue;
            }
            std::perror("read");
            return;
        }
    }
}

// Главный цикл: принимаем ОДНО соединение, обслуживаем его полностью,
// закрываем и только потом возвращаемся к accept() за следующим.
void run_server() {
    const FileDescriptor listener = make_listener(kPort);
    std::cout << "q01: синхронный сервер слушает порт " << kPort
              << " (один клиент за раз)\n";

    for (;;) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);  // корректный тип для accept
        FileDescriptor client{::accept(
            listener.get(), reinterpret_cast<sockaddr*>(&client_addr), &addr_len)};
        if (!client.valid()) {
            if (errno == EINTR) {
                continue;  // прервано сигналом — повторяем
            }
            std::perror("accept");
            continue;  // не валим сервер из-за одного клиента
        }

        std::array<char, INET_ADDRSTRLEN> ip{};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip.data(), ip.size());
        std::cout << "подключился " << ip.data() << ':'
                  << ntohs(client_addr.sin_port)
                  << " — обслуживаю (остальные ждут)\n";

        echo_loop(client.get());

        std::cout << "клиент " << ip.data() << ':' << ntohs(client_addr.sin_port)
                  << " отключён, готов принять следующего\n";
    }
}

}  // namespace

int main() {
    try {
        run_server();
    } catch (const std::system_error& error) {
        std::cerr << "фатальная ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
