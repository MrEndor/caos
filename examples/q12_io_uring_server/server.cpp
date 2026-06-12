// Вопрос 12: echo-сервер на io_uring (liburing).
//
// io_uring — асинхронный интерфейс ввода-вывода Linux на основе двух
// кольцевых очередей в разделяемой с ядром памяти:
//   SQ (submission queue)  — мы кладём запросы (SQE: "прими", "прочитай"...)
//   CQ (completion queue)  — ядро кладёт результаты (CQE).
//
// МОДЕЛЬ COMPLETION (а не readiness как у epoll):
//   epoll говорит "сокет ГОТОВ к чтению" — read делаешь сам отдельным syscall.
//   io_uring говорит "чтение УЖЕ ВЫПОЛНЕНО, вот байты" — само действие сделало
//   ядро. Меньше системных вызовов: один io_uring_submit() может отправить
//   пачку операций, completion'ы тоже забираются пачкой.
//
// ПРЕИМУЩЕСТВА над epoll:
//   + нет syscall на каждую операцию (batching submit/complete);
//   + единый механизм для сокетов, файлов, fsync и т.п.;
//   + multishot-операции (одна заявка -> много completion'ов);
//   + возможен polled/SQPOLL режим почти без syscall'ов вообще.
// НЕДОСТАТКИ:
//   - сложнее модель (управление lifetime буферов/состояний по user_data);
//   - молодой API => богатая история CVE (в т.ч. LPE), из-за чего его местами
//     отключают политиками (seccomp, io_uring_disabled sysctl).
//
// Здесь использован MULTISHOT ACCEPT (io_uring_prep_multishot_accept):
//   одна заявка на accept остаётся "вооружённой" и выдаёт CQE на КАЖДОЕ новое
//   соединение, пока в флагах CQE стоит IORING_CQE_F_MORE. Это избавляет от
//   ручного re-arm после каждого accept. Если F_MORE пропал — заявку
//   перевыставляем.
//
// user_data: на каждый сокет/операцию заводим heap-структуру Connection с полем
// type (kAccept/kRecv/kSend) и fd. Указатель кладём в SQE как user_data и
// получаем обратно из CQE -> так различаем, что именно завершилось. Время жизни
// объектов управляется std::unique_ptr; "сырой" указатель в kernel — наблюдатель.
//
// Структура файла: namespace net (RAII fd/ring, listener, get_sqe) +
// IoUringEchoServer (Connection-машина состояний на SQE/CQE).
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q12_io_uring_server/server.cpp -o bin/q12_server -luring
// run:     ./bin/q12_server     (nc 127.0.0.1 8080)

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// ───────────────────────── слой сокетов / io_uring ─────────────────────────
namespace net {

constexpr int kBacklog = 128;

// RAII-обёртка над файловым дескриптором: close() в деструкторе.
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

private:
    void reset() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_ = -1;
};

// RAII-обёртка над кольцом io_uring: io_uring_queue_exit() в деструкторе.
class Ring {
public:
    explicit Ring(unsigned depth) {
        // Возвращает -errno (а не ставит errno).
        const int result = ::io_uring_queue_init(depth, &ring_, 0);
        if (result < 0) {
            throw std::system_error{-result, std::generic_category(),
                                    "io_uring_queue_init"};
        }
    }

    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;

    Ring(Ring&& other) noexcept
        : ring_{other.ring_}, owned_{std::exchange(other.owned_, false)} {}

    Ring& operator=(Ring&& other) noexcept {
        if (this != &other) {
            reset();
            ring_ = other.ring_;
            owned_ = std::exchange(other.owned_, false);
        }
        return *this;
    }

    ~Ring() { reset(); }

    [[nodiscard]] io_uring* get() noexcept { return &ring_; }

private:
    void reset() noexcept {
        if (owned_) {
            ::io_uring_queue_exit(&ring_);
            owned_ = false;
        }
    }

    io_uring ring_{};
    bool     owned_ = true;
};

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error{errno, std::generic_category(), what};
}

// Создать listen-сокет (блокирующий — accept делает io_uring).
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
    return listener;
}

// Получить SQE или бросить исключение (SQ переполнена).
io_uring_sqe* get_sqe(io_uring* ring, const char* context) {
    io_uring_sqe* sqe = ::io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        throw std::runtime_error{std::string{"SQ переполнена: "} + context};
    }
    return sqe;
}

}  // namespace net

// ───────────────────────── сервер ─────────────────────────
namespace {

// Echo-сервер на io_uring: владеет listen-сокетом и кольцом. Состояние каждой
// операции живёт в heap-объекте Connection, чей указатель ездит в user_data
// туда-обратно через kernel-land; run() забирает CQE и крутит конечный автомат
// accept -> recv -> send -> recv ...
class IoUringEchoServer {
public:
    explicit IoUringEchoServer(std::uint16_t port)
        : listener_{net::make_listener(port)}, ring_{kQueueDepth}, port_{port} {
        // Вооружаем первый multishot-accept и проталкиваем его в ядро.
        submit_accept(std::make_unique<Connection>());
        const int submitted = ::io_uring_submit(ring_.get());
        if (submitted < 0) {
            throw std::system_error{-submitted, std::generic_category(),
                                    "io_uring_submit"};
        }
    }

    void run() {
        std::cout << "q12: io_uring сервер слушает порт " << port_
                  << " (multishot accept)\n";
        for (;;) {
            io_uring_cqe* cqe = nullptr;
            // Блокируемся, пока не появится хотя бы один готовый CQE.
            const int result = ::io_uring_wait_cqe(ring_.get(), &cqe);
            if (result < 0) {
                if (result == -EINTR) {
                    continue;
                }
                std::cerr << "io_uring_wait_cqe: " << std::strerror(-result) << '\n';
                break;
            }
            handle_completion(cqe);
            ::io_uring_cqe_seen(ring_.get(), cqe);
        }
    }

private:
    static constexpr unsigned    kQueueDepth = 256;
    static constexpr std::size_t kBufferSize = 4096;

    enum class OpType {
        kAccept,
        kRecv,
        kSend,
    };

    // Состояние одной операции/соединения. Указатель на неё едет в user_data SQE.
    // Для kAccept fd = listen_fd; для kRecv/kSend fd = клиентский сокет.
    struct Connection {
        OpType type = OpType::kAccept;
        int fd = -1;
        std::size_t length = 0;  // для kSend — сколько всего байт echo
        std::size_t sent = 0;    // для kSend — сколько уже отправлено
        std::array<char, kBufferSize> buffer{};
    };

    // ── постановка операций (владение conn уходит в kernel-land) ──

    // Вооружить multishot-accept на listen-сокете.
    void submit_accept(std::unique_ptr<Connection> conn) {
        io_uring_sqe* sqe = net::get_sqe(ring_.get(), "accept");
        conn->type = OpType::kAccept;
        conn->fd = listener_.get();
        // addr/addrlen = nullptr: адрес клиента нам не нужен.
        ::io_uring_prep_multishot_accept(sqe, listener_.get(), nullptr, nullptr, 0);
        ::io_uring_sqe_set_data(sqe, conn.release());
    }

    // Поставить RECV на клиентский сокет.
    void submit_recv(std::unique_ptr<Connection> conn) {
        io_uring_sqe* sqe = net::get_sqe(ring_.get(), "recv");
        conn->type = OpType::kRecv;
        ::io_uring_prep_recv(sqe, conn->fd, conn->buffer.data(),
                             conn->buffer.size(), 0);
        ::io_uring_sqe_set_data(sqe, conn.release());
    }

    // Поставить SEND (echo). Отправляем buffer[sent .. length).
    void submit_send(std::unique_ptr<Connection> conn) {
        io_uring_sqe* sqe = net::get_sqe(ring_.get(), "send");
        conn->type = OpType::kSend;
        ::io_uring_prep_send(sqe, conn->fd, conn->buffer.data() + conn->sent,
                             conn->length - conn->sent, 0);
        ::io_uring_sqe_set_data(sqe, conn.release());
    }

    // Снова вооружить accept после потери multishot (нет IORING_CQE_F_MORE).
    void rearm_accept() {
        submit_accept(std::make_unique<Connection>());
        ::io_uring_submit(ring_.get());
    }

    // ── обработка completion'ов ──

    void handle_accept(std::unique_ptr<Connection> accept_conn,
                       int result, unsigned flags) {
        if (result >= 0) {
            // result = новый клиентский дескриптор. Заводим состояние, ставим RECV.
            auto client = std::make_unique<Connection>();
            client->fd = result;
            try {
                submit_recv(std::move(client));
                ::io_uring_submit(ring_.get());
            } catch (const std::exception& error) {
                std::cerr << "submit_recv: " << error.what() << '\n';
                ::close(result);
            }
        } else {
            std::cerr << "accept: " << std::strerror(-result) << '\n';
        }

        // IORING_CQE_F_MORE: заявка accept ещё "жива", повторно НЕ ставим.
        // Если флага нет — multishot завершился, перевооружаем новым объектом.
        if (flags & IORING_CQE_F_MORE) {
            // Заявка жива: возвращаем владение обратно в kernel-land (тот же объект).
            (void)accept_conn.release();
        } else {
            accept_conn.reset();  // старый объект больше не нужен
            rearm_accept();
        }
    }

    void handle_recv(std::unique_ptr<Connection> conn, int result) {
        if (result > 0) {
            // Прочитали result байт -> отправляем их обратно (echo).
            conn->length = static_cast<std::size_t>(result);
            conn->sent = 0;
            const int client_fd = conn->fd;
            try {
                submit_send(std::move(conn));
                ::io_uring_submit(ring_.get());
            } catch (const std::exception& error) {
                std::cerr << "submit_send: " << error.what() << '\n';
                ::close(client_fd);
            }
            return;
        }
        // result == 0 -> EOF (клиент закрылся); result < 0 -> ошибка.
        if (result < 0 && result != -ECONNRESET) {
            std::cerr << "recv: " << std::strerror(-result) << '\n';
        }
        ::close(conn->fd);  // conn освободится при выходе из функции
    }

    void handle_send(std::unique_ptr<Connection> conn, int result) {
        if (result < 0) {
            if (result != -EPIPE && result != -ECONNRESET) {
                std::cerr << "send: " << std::strerror(-result) << '\n';
            }
            ::close(conn->fd);
            return;
        }

        conn->sent += static_cast<std::size_t>(result);
        const int client_fd = conn->fd;
        try {
            if (conn->sent < conn->length) {
                // Частичная отправка — дослать остаток (partial-write).
                submit_send(std::move(conn));
            } else {
                // Отправили всё -> снова ждём данные от клиента.
                submit_recv(std::move(conn));
            }
            ::io_uring_submit(ring_.get());
        } catch (const std::exception& error) {
            std::cerr << "resubmit: " << error.what() << '\n';
            ::close(client_fd);
        }
    }

    // Разобрать один CQE и направить в нужный обработчик.
    void handle_completion(io_uring_cqe* cqe) {
        auto* raw = static_cast<Connection*>(::io_uring_cqe_get_data(cqe));
        const int result = cqe->res;       // результат операции (как у syscall)
        const unsigned flags = cqe->flags;
        if (raw == nullptr) {
            return;  // не должно случаться — защитимся
        }

        // Принимаем владение объектом из kernel-land обратно в C++.
        std::unique_ptr<Connection> conn{raw};
        switch (conn->type) {
            case OpType::kAccept:
                handle_accept(std::move(conn), result, flags);
                break;
            case OpType::kRecv:
                handle_recv(std::move(conn), result);
                break;
            case OpType::kSend:
                handle_send(std::move(conn), result);
                break;
        }
    }

    net::FileDescriptor listener_;
    net::Ring           ring_;
    std::uint16_t       port_;
};

}  // namespace

int main() try {
    IoUringEchoServer server{8080};
    server.run();
} catch (const std::exception& error) {
    std::cerr << "фатальная ошибка: " << error.what() << '\n';
    return 1;
}
