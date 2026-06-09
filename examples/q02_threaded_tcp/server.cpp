// Вопрос 2: многопоточный TCP echo-сервер (thread-per-connection).
//
// На каждое принятое соединение создаём отдельный std::thread и detach()'им его.
// Теперь клиенты обслуживаются ПАРАЛЛЕЛЬНО: главный поток сразу возвращается
// к accept(), пока worker'ы echo-ят своих клиентов.
//
// Тонкости, которые показывает вопрос:
//  - владение клиентским fd передаётся в поток ПЕРЕМЕЩЕНИЕМ RAII-обёртки
//    (std::move в лямбду). Так каждый поток владеет своим дескриптором, и нет
//    гонки за общей переменной, которую main мог бы перезаписать след. accept().
//  - thread.detach() — поток сам освобождает свои ресурсы при завершении,
//    join делать не нужно (иначе main блокировался бы на каждом клиенте).
//
// Минус модели: на 10k клиентов = 10k потоков (по ~8 МБ стека + контекст-свитчи).
// Это мотивация для пулов потоков (q05) и event-loop (q10/q11/q12).
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q02_threaded_tcp/server.cpp -o bin/q02_server -pthread
// run:     ./bin/q02_server     (клиент: ./bin/q02_client "hello")

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <system_error>
#include <thread>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 8080;
constexpr int kBacklog = 128;
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

// Создать, привязать и перевести в режим прослушивания TCP-сокет на порту.
FileDescriptor make_listener(std::uint16_t port) {
    FileDescriptor listener{::socket(AF_INET, SOCK_STREAM, 0)};
    if (!listener.valid()) {
        throw_errno("socket");
    }

    // SO_REUSEADDR: избегаем EADDRINUSE при быстром рестарте (TIME_WAIT).
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
    return listener;
}

// Записать ровно count байт, обрабатывая частичную запись и EINTR.
bool write_all(int client_fd, const char* data, std::size_t count) {
    std::size_t written = 0;
    while (written < count) {
        const ssize_t chunk = ::write(client_fd, data + written, count - written);
        if (chunk < 0) {
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

// Тело worker-потока: владеет client (по значению — перемещено сюда),
// делает echo и закрывает сокет автоматически в деструкторе FileDescriptor.
void handle_client(FileDescriptor client) {
    std::array<char, kBufferSize> buffer{};
    for (;;) {
        const ssize_t bytes_read = ::read(client.get(), buffer.data(), buffer.size());
        if (bytes_read > 0) {
            if (!write_all(client.get(), buffer.data(),
                           static_cast<std::size_t>(bytes_read))) {
                return;
            }
        } else if (bytes_read == 0) {
            return;  // EOF — клиент закрыл соединение
        } else {
            if (errno == EINTR) {
                continue;
            }
            std::perror("read");
            return;
        }
    }
}

// Главный цикл: accept -> запуск detached-потока на каждого клиента.
void run_server() {
    const FileDescriptor listener = make_listener(kPort);
    std::cout << "q02: многопоточный сервер слушает порт " << kPort
              << " (поток на клиента)\n";

    for (;;) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        FileDescriptor client{::accept(
            listener.get(), reinterpret_cast<sockaddr*>(&client_addr), &addr_len)};
        if (!client.valid()) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("accept");
            continue;
        }

        try {
            // Передаём владение fd в поток через std::move — никакой гонки за
            // общей переменной. detach: поток сам уберёт за собой при выходе.
            std::thread worker{handle_client, std::move(client)};
            worker.detach();
        } catch (const std::system_error& error) {
            // Не удалось создать поток — client закроется в деструкторе.
            std::cerr << "не удалось создать поток: " << error.what() << '\n';
        }
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
