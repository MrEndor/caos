// Вопрос 11: echo-сервер на epoll (edge-triggered, ET).
//
// epoll решает обе проблемы busy-loop (q10):
//   - epoll_wait() СПИТ, пока нет событий -> не жжёт CPU вхолостую;
//   - возвращает только ГОТОВЫЕ дескрипторы -> O(числа активных), а не O(N).
//
// Здесь выбран EDGE-TRIGGERED режим (EPOLLET). Почему ET и почему drain-loop:
//   ET уведомляет о событии РОВНО ОДИН раз — в момент ПЕРЕХОДА буфера из
//   "пусто" в "есть данные". Если мы прочитаем не всё, повторного уведомления
//   НЕ будет, и остаток "зависнет". Поэтому при ET ОБЯЗАТЕЛЬНО:
//     1) читать в цикле, пока read не вернёт EAGAIN (буфер опустошён);
//     2) accept'ить в цикле, пока не EAGAIN (могло прийти несколько коннектов);
//     3) сокеты должны быть НЕблокирующими, иначе финальный read/accept,
//        опустошив буфер, заблокирует весь поток.
// (Level-triggered, LT — режим по умолчанию — проще: уведомляет, пока есть
//  данные, можно читать по чуть-чуть; но даёт больше пробуждений.)
//
// ── Масштабирование на несколько потоков (по теме "многопоточный epoll") ──
// Этот пример однопоточный. Промышленные варианты:
//   (а) N воркеров, у КАЖДОГО свой epoll, общий listen-сокет создан с
//       SO_REUSEPORT — ядро само раскидывает входящие коннекты по сокетам,
//       устраняя "thundering herd";
//   (б) один listen-сокет, добавленный во все epoll'ы с флагом EPOLLEXCLUSIVE
//       — ядро будит лишь ОДИН поток на событие accept.
// Логику воркера (epoll_wait + обработка) при этом просто запускают в каждом
// std::thread. Здесь оставлен один поток для ясности сути epoll.
//
// Структура файла: namespace net (слой сокетов) + EpollEchoServer.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q11_epoll_server/server.cpp -o bin/q11_server
// run:     ./bin/q11_server     (nc 127.0.0.1 8080)

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <system_error>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// ───────────────────────── слой сокетов ─────────────────────────
namespace net {

constexpr int kBacklog = 128;

// RAII-обёртка над файловым дескриптором: close() в деструкторе.
// Используется и для обычных сокетов, и для epoll-fd. release() отдаёт
// владение наружу (нужно, когда fd начинает жить в epoll-наборе).
class FileDescriptor {
public:
    FileDescriptor() = default;

    explicit FileDescriptor(int fd) noexcept : fd_{fd} {}

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept
        : fd_{std::exchange(other.fd_, -1)} {}

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~FileDescriptor() { reset(); }

    [[nodiscard]] int  get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    // Отдать владение дескриптором наружу, не закрывая его.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

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

// Создать listen-сокет в неблокирующем режиме (нужно для ET accept-loop).
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
// Возвращает false при ошибке/переполнении буфера (соединение закрываем).
bool write_all(int client_fd, const char* data, std::size_t count) {
    std::size_t written = 0;
    while (written < count) {
        const ssize_t chunk = ::write(client_fd, data + written, count - written);
        if (chunk < 0) {
            if (errno == EINTR) {
                continue;
            }
            // EAGAIN на write = send-буфер полон. В полном сервере остаток
            // буферизуют и ждут EPOLLOUT; в примере просто закрываем соединение.
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

}  // namespace net

// ───────────────────────── сервер ─────────────────────────
namespace {

// Echo-сервер на epoll в edge-triggered режиме. Владеет listen-сокетом и
// epoll-дескриптором; run() спит в epoll_wait и обрабатывает готовые fd.
class EpollEchoServer {
public:
    explicit EpollEchoServer(std::uint16_t port)
        : listener_{net::make_listener(port)},
          epoll_fd_{::epoll_create1(0)},  // epoll_create1(0): современный аналог epoll_create
          port_{port} {
        if (!epoll_fd_.valid()) {
            net::throw_errno("epoll_create1");
        }
        // Регистрируем listen-сокет: EPOLLIN (есть кого accept) | EPOLLET.
        if (!add_to_epoll(listener_.get(), EPOLLIN | EPOLLET)) {
            throw std::system_error{errno, std::generic_category(),
                                    "epoll_ctl(ADD listen)"};
        }
    }

    void run() {
        std::cout << "q11: epoll (ET) сервер слушает порт " << port_ << '\n';
        std::array<epoll_event, kMaxEvents> events{};
        for (;;) {
            // -1 = ждать бесконечно; поток спит, пока ядро не разбудит событием.
            const int ready =
                ::epoll_wait(epoll_fd_.get(), events.data(), kMaxEvents, -1);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;  // прервано сигналом
                }
                std::perror("epoll_wait");
                break;
            }
            for (int i = 0; i < ready; ++i) {
                handle_event(events[static_cast<std::size_t>(i)]);
            }
        }
    }

private:
    static constexpr int         kMaxEvents = 64;
    static constexpr std::size_t kBufferSize = 4096;

    // Зарегистрировать дескриптор в epoll с заданными событиями.
    // В data.fd кладём сам дескриптор для опознания при epoll_wait().
    bool add_to_epoll(int fd, std::uint32_t events) {
        epoll_event event{};
        event.events = events;
        event.data.fd = fd;
        if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &event) < 0) {
            std::perror("epoll_ctl(ADD)");
            return false;
        }
        return true;
    }

    // Снять дескриптор с epoll (перед close).
    void remove_from_epoll(int fd) {
        ::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    }

    // ET accept-loop: принимаем ВСЕ ожидающие коннекты, пока accept не вернёт
    // EAGAIN. При ET одно уведомление = надо вычерпать backlog полностью.
    void accept_all() {
        for (;;) {
            net::FileDescriptor client{::accept(listener_.get(), nullptr, nullptr)};
            if (!client.valid()) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                std::perror("accept");
                break;
            }
            if (!net::set_nonblocking(client.get())) {
                continue;  // client закроется в деструкторе
            }
            if (add_to_epoll(client.get(), EPOLLIN | EPOLLET)) {
                // fd теперь живёт в epoll-наборе: отдаём владение, чтобы деструктор
                // не закрыл живой сокет. close сделаем вручную при EOF/ошибке.
                (void)client.release();
            }
        }
    }

    // ET read drain-loop: читаем, пока не EAGAIN. Если прочитать не всё —
    // повторного EPOLLIN не будет! Возвращает true, если соединение надо закрыть.
    static bool drain_client(int client_fd) {
        std::array<char, kBufferSize> buffer{};
        for (;;) {
            const ssize_t bytes_read =
                ::read(client_fd, buffer.data(), buffer.size());
            if (bytes_read > 0) {
                if (!net::write_all(client_fd, buffer.data(),
                                    static_cast<std::size_t>(bytes_read))) {
                    return true;  // ошибка записи — закрываем
                }
            } else if (bytes_read == 0) {
                return true;  // EOF — клиент закрыл соединение
            } else {
                // EAGAIN -> буфер опустошён, выходим из drain-loop.
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return false;
                }
                if (errno == EINTR) {
                    continue;
                }
                std::perror("read");
                return true;
            }
        }
    }

    // Обработать одно готовое событие epoll.
    void handle_event(const epoll_event& event) {
        const int fd = event.data.fd;

        // Ошибка/закрытие на дескрипторе.
        if (event.events & (EPOLLERR | EPOLLHUP)) {
            remove_from_epoll(fd);
            ::close(fd);
            return;
        }

        if (fd == listener_.get()) {
            accept_all();
            return;
        }

        if (drain_client(fd)) {
            remove_from_epoll(fd);
            ::close(fd);
        }
    }

    net::FileDescriptor listener_;
    net::FileDescriptor epoll_fd_;
    std::uint16_t       port_;
};

}  // namespace

int main() {
    try {
        EpollEchoServer server{8080};
        server.run();
    } catch (const std::system_error& error) {
        std::cerr << "фатальная ошибка: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
