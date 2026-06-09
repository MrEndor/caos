// Вопрос 2: простой TCP-клиент для echo-сервера.
//
// socket() -> connect(127.0.0.1:8080) -> send(строка) -> recv(echo) -> print.
// Строку берём из argv[1], по умолчанию "hello".
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q02_threaded_tcp/client.cpp -o bin/q02_client
// run:     ./bin/q02_client "сообщение"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 8080;
constexpr std::size_t kBufferSize = 4096;
constexpr const char* kServerIp = "127.0.0.1";

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

// Создать сокет и подключиться к host:port (IPv4).
FileDescriptor connect_to(const char* host, std::uint16_t port) {
    FileDescriptor sock{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!sock.valid()) {
        throw_errno("socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // inet_pton: текстовый IP -> бинарный. 1 при успехе, 0 если строка не
    // валидный адрес, -1 при ошибке семейства.
    const int parsed = ::inet_pton(AF_INET, host, &address.sin_addr);
    if (parsed == 0) {
        throw std::system_error{EINVAL, std::generic_category(),
                                "inet_pton: неверный адрес"};
    }
    if (parsed < 0) {
        throw_errno("inet_pton");
    }

    if (::connect(sock.get(), reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) < 0) {
        throw_errno("connect");
    }
    return sock;
}

// Записать всё сообщение целиком (partial-write цикл, обработка EINTR).
void send_all(int sock_fd, std::string_view message) {
    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t chunk =
            ::write(sock_fd, message.data() + sent, message.size() - sent);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("write");
        }
        sent += static_cast<std::size_t>(chunk);
    }
}

// Прочитать echo-ответ длиной до expected байт.
// Ответ может прийти несколькими read'ами — читаем, пока не наберём expected.
std::string receive_echo(int sock_fd, std::size_t expected) {
    std::array<char, kBufferSize> buffer{};
    std::size_t received = 0;
    const std::size_t limit = std::min(expected, buffer.size());
    while (received < limit) {
        const ssize_t chunk =
            ::read(sock_fd, buffer.data() + received, buffer.size() - received);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("read");
        }
        if (chunk == 0) {
            break;  // сервер закрыл соединение
        }
        received += static_cast<std::size_t>(chunk);
    }
    return std::string{buffer.data(), received};
}

// Отправить сообщение и вернуть полученный echo-ответ.
std::string send_and_receive(int sock_fd, std::string_view message) {
    send_all(sock_fd, message);
    return receive_echo(sock_fd, message.size());
}

}  // namespace

int main(int argc, char** argv) {
    const std::string message = (argc > 1) ? argv[1] : "hello";
    try {
        const FileDescriptor sock = connect_to(kServerIp, kPort);
        const std::string echo = send_and_receive(sock.get(), message);
        std::cout << "echo (" << echo.size() << " байт): " << echo << '\n';
    } catch (const std::system_error& error) {
        std::cerr << "ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
