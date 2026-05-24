# Сети

Сетевое программирование в Linux построено вокруг сокетов — расширения файлового интерфейса на межпроцессное и межмашинное взаимодействие. Сокет — это file descriptor, к которому применимы знакомые `read`, `write`, `close`; разница только в том, что данные идут не на диск, а через сетевой стек ядра в другой процесс на этом или другом хосте.

Этот раздел рассказывает, как устроен Berkeley sockets API, чем TCP отличается от UDP под капотом и какие тонкости транспортного уровня нужно знать для написания корректных сетевых приложений.

## Темы

| Страница | Что рассматривается |
|----------|---------------------|
| [Сокеты: API и базовые понятия](sockets_basics.md) | `socket`, `bind`, `listen`, `accept`, `connect`, address family, byte order, echo-сервер/клиент |
| [TCP и UDP: протоколы и тонкости](tcp_udp.md) | Three-way handshake, state machine, TIME_WAIT, flow/congestion control, Nagle, MSS, SO_REUSEPORT |
| [I/O multiplexing (select, poll, epoll)](io_multiplexing.md) | `fd_set`, `pollfd`, edge-triggered vs level-triggered, C10K, внутреннее устройство epoll |
| [io_uring](io_uring.md) | Submission/completion queues, SQE/CQE, SQPOLL, fixed buffers, linked SQEs, liburing |
