# Сокеты: API и базовые понятия

Socket — universal endpoint для I/O между процессами на одной или разных машинах. С точки зрения процесса это обычный
file descriptor: те же `read(2)`, `write(2)`, `close(2)`, что и для файлов. Разница — на другом конце не блочное
устройство, а сетевой стек kernel'а, который доставит байты по TCP/IP, UDP или локальному Unix-сокету.

Berkeley sockets появились в 4.2BSD (1983) и стали дефакто-стандартом сетевого API. POSIX практически целиком
унаследовал
интерфейс; Windows Winsock — тоже клон Berkeley с косметическими отличиями (`closesocket` вместо `close`, `WSAStartup`
при инициализации). Поэтому код, написанный под Linux, переносится между UNIX-системами почти без изменений.

## Виды сокетов

Сокет создаётся одним вызовом:

```c
int sockfd = socket(domain, type, protocol);
```

**Domain (address family)** определяет, какой адрес умеет ставить kernel:

| Константа    | Адресное пространство   | Назначение                                 |
|--------------|-------------------------|--------------------------------------------|
| `AF_INET`    | IPv4 (4 байта + port)   | TCP/UDP через IPv4                         |
| `AF_INET6`   | IPv6 (16 байт + port)   | TCP/UDP через IPv6                         |
| `AF_UNIX`    | путь в файловой системе | IPC между процессами одной машины          |
| `AF_PACKET`  | raw Ethernet frame      | сниффинг, свои L2-протоколы, `CAP_NET_RAW` |
| `AF_NETLINK` | kernel ↔ userspace      | управление маршрутизацией, ip, audit       |

**Type** — семантика доставки:

| Константа        | Гарантии                                            | Типичный протокол |
|------------------|-----------------------------------------------------|-------------------|
| `SOCK_STREAM`    | reliable, ordered, byte-stream, connection-oriented | TCP               |
| `SOCK_DGRAM`     | message-oriented, без гарантий доставки и порядка   | UDP               |
| `SOCK_RAW`       | свой протокол поверх IP, требует `CAP_NET_RAW`      | ICMP, custom      |
| `SOCK_SEQPACKET` | reliable, ordered, но message-oriented              | SCTP, AF_UNIX     |

**Protocol** — обычно `0` (default для пары domain+type). Указывать явно нужно, когда внутри одной семьи возможно
несколько протоколов: для `SOCK_RAW` это `IPPROTO_ICMP`, `IPPROTO_TCP` и так далее.

Комбинация `(AF_INET, SOCK_STREAM, 0)` даёт TCP-сокет, `(AF_INET, SOCK_DGRAM, 0)` — UDP, `(AF_UNIX, SOCK_STREAM, 0)` —
надёжный локальный канал. Несовместимые сочетания (например, `AF_PACKET` + `SOCK_STREAM`) kernel отклонит ошибкой
`ESOCKTNOSUPPORT`.

## Адреса

Каждой address family соответствует своя структура адреса. Чтобы все API работали единообразно, они объявлены
бинарно-совместимыми с generic `struct sockaddr`:

```c
struct sockaddr {
    sa_family_t sa_family;   // AF_INET / AF_INET6 / AF_UNIX / ...
    char        sa_data[14]; // payload, интерпретируется по family
};
```

Все системные вызовы (`bind`, `connect`, `accept`, `sendto`, ...) принимают `struct sockaddr*` и `socklen_t addrlen` —
размер реальной структуры. Kernel смотрит на `sa_family` и сам кастит указатель к нужному типу. Так один и тот же
`bind(2)` обслуживает IPv4, IPv6 и Unix-сокеты.

**IPv4-адрес** (`struct sockaddr_in`, 16 байт):

```
            ┌────────────────────────────────────────┐
offset 0  ─▶│ sin_family   (2 байта, AF_INET)        │
            ├────────────────────────────────────────┤
offset 2  ─▶│ sin_port     (2 байта, big-endian)     │
            ├────────────────────────────────────────┤
offset 4  ─▶│ sin_addr     (4 байта, big-endian)     │
            ├────────────────────────────────────────┤
offset 8  ─▶│ sin_zero[8]  (padding до sockaddr size)│
            └────────────────────────────────────────┘
```

`sin_zero` нужен только чтобы размер совпадал с `struct sockaddr` — должен быть обнулён.

**IPv6** (`struct sockaddr_in6`, 28 байт) дополнительно хранит `sin6_flowinfo` и `sin6_scope_id`.
**Unix-сокет** (`struct sockaddr_un`) — путь до 108 байт в поле `sun_path`.

Типичный паттерн заполнения IPv4-адреса:

```c
struct sockaddr_in addr = {0};
addr.sin_family = AF_INET;
addr.sin_port   = htons(8080);
inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
```

Каст `(struct sockaddr*)&addr` — норма, не нарушение strict aliasing: glibc объявляет эти типы совместимыми через
расширения POSIX и `__attribute__((__transparent_union__))` в части прототипов.

## Byte order

Сеть исторически использует big-endian (старший байт первым) — это назвали **network byte order**. x86 и ARM
работают в little-endian, поэтому каждое числовое поле в сетевых структурах нужно конвертировать вручную:

```
host (x86, little-endian)              network (big-endian)
uint16_t port = 8080                   0x1F90 на проводе
байты в памяти: 0x90 0x1F   ──htons──▶  байты: 0x1F 0x90
```

Семейство макросов:

| Функция   | Расшифровка           | Размер  |
|-----------|-----------------------|---------|
| `htons()` | host to network short | 16 бит  |
| `htonl()` | host to network long  | 32 бита |
| `ntohs()` | network to host short | 16 бит  |
| `ntohl()` | network to host long  | 32 бита |

На big-endian машинах эти макросы — no-op, на little-endian — `__builtin_bswap16/32`. Забыть `htons(port)` — частая
ошибка: kernel прочитает 8080 как 0x901F = 36895, и сервер «послушает не тот порт».

Текстовые IP-адреса конвертируются через `inet_pton` / `inet_ntop` (presentation ↔ network):

```c
struct in_addr ipv4;
inet_pton(AF_INET, "192.168.1.1", &ipv4);    // строка → 4 байта

char buf[INET_ADDRSTRLEN];
inet_ntop(AF_INET, &ipv4, buf, sizeof(buf)); // 4 байта → "192.168.1.1"
```

Старые `inet_addr` / `inet_ntoa` не поддерживают IPv6 и плохо сообщают об ошибках — в новом коде их не используют.

## Жизненный цикл сервера

```
   socket()        bind()          listen()         accept()
      │              │                │                │
      ▼              ▼                ▼                ▼
  ┌───────┐     ┌────────┐      ┌──────────┐     ┌──────────┐
  │ alloc │ ──▶ │ bind   │ ───▶ │ backlog  │ ──▶ │ блокир., │
  │  fd   │     │ addr + │      │ queue    │     │ ждём SYN │
  │ + sb  │     │ port   │      │ allocate │     │          │
  └───────┘     └────────┘      └──────────┘     └────┬─────┘
                                                      │
                                                      ▼
                                                ┌──────────────┐
                                                │ new client_fd│
                                                │ для каждого  │
                                                │ соединения   │
                                                └──────┬───────┘
                                                       │
                                       ┌───────────────┼───────────────┐
                                       ▼               ▼               ▼
                                  recv/send       recv/send         close()
                                    loop            loop          (client_fd)
```

**`socket()`** — kernel выделяет `struct socket`, `struct sock` (TCP-специфичный control block) и регистрирует fd в
fdtable процесса. Никакой адресации ещё нет.

**`bind()`** — закрепляет за сокетом локальный `(IP, port)`. Без bind kernel назначит эфемерный порт автоматически при
первой отправке, но серверу нужен предсказуемый. Возможные ошибки: `EADDRINUSE` (порт уже занят или висит в TIME_WAIT),
`EACCES` (порт < 1024 без `CAP_NET_BIND_SERVICE`), `EADDRNOTAVAIL` (нет такого IP у машины).

**`listen(fd, backlog)`** — переводит сокет в passive-mode. Kernel создаёт две очереди: SYN queue (полузавершённые
handshake) и accept queue (полностью установленные, ждут `accept`). `backlog` ограничивает accept queue, но реальный
лимит — `min(backlog, /proc/sys/net/core/somaxconn)`. Слишком маленький backlog при всплеске трафика приводит к
`ECONNRESET` у клиентов.

**`accept()`** — снимает первое готовое соединение из accept queue, возвращает **новый** fd для общения с этим клиентом.
Слушающий сокет остаётся открытым для следующих `accept`. Если очередь пуста — блокировка (или `EAGAIN` для
`O_NONBLOCK`).

## Жизненный цикл клиента

```
   socket()         connect()              send/recv loop          close()
      │                │                          │                   │
      ▼                ▼                          ▼                   ▼
  ┌───────┐      ┌──────────┐               ┌──────────┐         ┌────────┐
  │ alloc │ ───▶ │ 3-way    │ ────────────▶ │ обмен    │ ──────▶ │ FIN +  │
  │  fd   │      │ handshake│               │ данными  │         │ TIME_  │
  │       │      │ SYN/     │               │          │         │ WAIT   │
  │       │      │ SYN-ACK/ │               │          │         │        │
  │       │      │ ACK      │               │          │         │        │
  └───────┘      └──────────┘               └──────────┘         └────────┘
```

`connect()` для TCP инициирует 3-way handshake (SYN → SYN-ACK → ACK) и возвращается, когда соединение установлено. Если
сервер не отвечает — `ETIMEDOUT` через несколько минут. Если сервер вернул RST (никто не слушает на порту) —
`ECONNREFUSED` сразу.

Для UDP `connect()` ничего не отправляет, только запоминает remote address: после этого можно использовать `send`/`recv`
вместо `sendto`/`recvfrom`, а пакеты с других адресов будут отбрасываться.

Bind на клиенте обычно не нужен — kernel сам выберет эфемерный порт из диапазона
`/proc/sys/net/ipv4/ip_local_port_range`
(по умолчанию 32768–60999).

## TCP echo server

```c
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT     8080
#define BACKLOG  16
#define BUF_SIZE 4096

int main(void) {
    // 1. Создаём слушающий сокет.
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    // 2. Без SO_REUSEADDR после рестарта получим EADDRINUSE из-за TIME_WAIT.
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 3. Привязываем к 0.0.0.0:8080 — слушаем на всех интерфейсах.
    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    // 4. Переходим в passive-mode, accept queue до BACKLOG соединений.
    if (listen(listen_fd, BACKLOG) < 0) { perror("listen"); return 1; }
    printf("listening on 0.0.0.0:%d\n", PORT);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);

        // 5. accept() блокируется до прихода клиента, отдаёт новый fd.
        int client_fd = accept(listen_fd, (struct sockaddr*)&cli, &cli_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;  // прервано сигналом
            perror("accept"); continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        printf("client %s:%d\n", ip, ntohs(cli.sin_port));

        // 6. Echo loop: читаем до EOF, шлём обратно.
        char buf[BUF_SIZE];
        ssize_t n;
        while ((n = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t k = send(client_fd, buf + sent, n - sent, MSG_NOSIGNAL);
                if (k < 0) { perror("send"); break; }
                sent += k;
            }
        }
        if (n < 0) perror("recv");

        close(client_fd);  // отправит FIN, перейдёт в TIME_WAIT
    }
}
```

Несколько тонкостей в коде выше:

- `recv` возвращает `0` ровно в одном случае — peer закрыл свою сторону (получили FIN). Любые другие данные — `n > 0`.
- `send` может записать меньше, чем попросили — отсюда внутренний цикл `while (sent < n)`. На блокирующем сокете это
  происходит редко, но возможно при сигналах или малом `SO_SNDBUF`.
- `MSG_NOSIGNAL` не даёт kernel'у отправить нам `SIGPIPE`, если клиент уже закрыл сокет — вместо смерти процесса
  получим `EPIPE` из `send`.
- Это **однопоточный** сервер: пока обрабатываем одного клиента, остальные ждут в accept queue. Для масштабирования —
  `fork`, threads или `epoll` (см. связанные темы).

Компиляция и запуск:

```bash
gcc -Wall -O2 echo_server.c -o echo_server
./echo_server
```

## TCP echo client

```c
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <ip> <port>\n", argv[0]); return 1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        fprintf(stderr, "bad ip\n"); return 1;
    }

    // 3-way handshake. ECONNREFUSED — на порту никто не слушает.
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }

    const char *msg = "hello, socket\n";
    if (send(fd, msg, strlen(msg), 0) < 0) { perror("send"); return 1; }

    char buf[256];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n < 0) { perror("recv"); return 1; }

    buf[n] = '\0';
    printf("got %zd bytes: %s", n, buf);

    close(fd);
    return 0;
}
```

## UDP echo: чем отличается

UDP — без соединений, без accept, каждый `recvfrom` сразу отдаёт пакет с адресом отправителя:

```c
// UDP echo server
int fd = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in addr = {0};
addr.sin_family      = AF_INET;
addr.sin_port        = htons(9090);
addr.sin_addr.s_addr = htonl(INADDR_ANY);
bind(fd, (struct sockaddr*)&addr, sizeof(addr));

for (;;) {
    char buf[2048];
    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);

    ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
                        (struct sockaddr*)&peer, &plen);
    if (n < 0) { perror("recvfrom"); continue; }

    sendto(fd, buf, n, 0, (struct sockaddr*)&peer, plen);
}
```

```c
// UDP echo client
int fd = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in srv = {0};
srv.sin_family = AF_INET;
srv.sin_port   = htons(9090);
inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

const char *msg = "ping";
sendto(fd, msg, strlen(msg), 0, (struct sockaddr*)&srv, sizeof(srv));

char buf[2048];
socklen_t slen = sizeof(srv);
ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&srv, &slen);
buf[n] = '\0';
printf("reply: %s\n", buf);
```

Ключевые отличия от TCP:

- Нет `listen`/`accept` — один сокет принимает датаграммы от всех клиентов.
- Граница датаграммы сохраняется: один `sendto` = один `recvfrom`. Если буфер меньше пакета, остаток молча отбрасывается
  (для `SOCK_DGRAM`), флаг `MSG_TRUNC` сигнализирует об этом.
- Нет гарантий доставки, порядка, защиты от дублирования — это забота приложения, если нужно.
- Максимальный безопасный размер payload без фрагментации — ~1472 байта (1500 MTU − 20 IP − 8 UDP). Большие пакеты IP
  фрагментирует, потеря одного фрагмента теряет всю датаграмму.

## Socket options

`setsockopt` / `getsockopt` управляют поведением сокета на разных уровнях:

```c
int yes = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
```

| Option         | Уровень       | Зачем                                                      |
|----------------|---------------|------------------------------------------------------------|
| `SO_REUSEADDR` | `SOL_SOCKET`  | bind на адрес, висящий в TIME_WAIT после рестарта          |
| `SO_REUSEPORT` | `SOL_SOCKET`  | несколько процессов на один порт, kernel балансирует SYN   |
| `SO_KEEPALIVE` | `SOL_SOCKET`  | пробинг неактивных TCP-соединений                          |
| `SO_LINGER`    | `SOL_SOCKET`  | поведение `close` при недослaнных данных                   |
| `SO_SNDBUF`    | `SOL_SOCKET`  | размер kernel send buffer                                  |
| `SO_RCVBUF`    | `SOL_SOCKET`  | размер kernel recv buffer                                  |
| `TCP_NODELAY`  | `IPPROTO_TCP` | отключить Nagle algorithm — отправлять мелкие пакеты сразу |
| `TCP_CORK`     | `IPPROTO_TCP` | копить данные до полного MSS                               |

`SO_REUSEADDR` и `SO_REUSEPORT` часто путают: первый разрешает повторный bind после TIME_WAIT (один процесс),
второй — параллельный bind несколькими процессами одновременно (kernel сам распределяет входящие соединения по
listening сокетам, давая бесплатный load balancing). Подробности — в статье про TCP/UDP.

## Частые errno

| Errno           | Где встречается    | Причина и реакция                                                      |
|-----------------|--------------------|------------------------------------------------------------------------|
| `EADDRINUSE`    | `bind`             | порт занят или TIME_WAIT — `SO_REUSEADDR` или другой порт              |
| `EADDRNOTAVAIL` | `bind`, `connect`  | у машины нет такого IP / эфемерные порты кончились                     |
| `ECONNREFUSED`  | `connect`          | на порту никто не слушает — kernel ответил RST                         |
| `ETIMEDOUT`     | `connect`, `recv`  | пакеты не дошли, retransmit'ы исчерпаны                                |
| `EPIPE`         | `send`/`write`     | peer закрыл сокет; ещё прилетит `SIGPIPE` (используйте `MSG_NOSIGNAL`) |
| `ECONNRESET`    | `recv`             | peer отправил RST (резкий close, краш)                                 |
| `EAGAIN`        | non-blocking I/O   | данных нет / буфер полон, попробовать позже                            |
| `EINTR`         | блокирующие вызовы | прервано сигналом — обычно retry в цикле                               |
| `EMSGSIZE`      | `sendto` (UDP)     | датаграмма больше MTU и DF выставлен                                   |
| `ENOTCONN`      | `send`, `recv`     | сокет не connected — забыли `connect` или соединение умерло            |

## Диагностика

```bash
# Открытые listening-сокеты с PID и именем процесса
ss -tlnp

# Все TCP-соединения с состояниями (ESTABLISHED, TIME_WAIT, ...)
ss -tan

# Старый аналог
netstat -tulpan

# Снифинг трафика на loopback по порту 8080
sudo tcpdump -i lo -A port 8080

# Быстрый тест клиента/сервера без своего кода
nc -l 8080              # listening сервер
nc 127.0.0.1 8080       # клиент

# UDP вариант
nc -u -l 9090
nc -u 127.0.0.1 9090
```

Полезно держать в голове состояния TCP: `ss -tan` показывает их в третьем столбце. `LISTEN` — слушающий сокет;
`ESTABLISHED` — рабочее соединение; `TIME_WAIT` — после нашего `close` (висит ~60 секунд по `2*MSL`); `CLOSE_WAIT` —
peer закрыл, мы ещё не вызвали `close` (накопление CLOSE_WAIT — баг приложения, утечка fd).

## Связанные темы

- [TCP и UDP: протоколы и тонкости](tcp_udp.md) — детали транспортного уровня, TIME_WAIT, congestion control, тюнинг
- [I/O multiplexing](../networking/io_multiplexing.md) — `epoll`/`select` для тысяч соединений в одном потоке
- [IPC](../processes/ipc.md) — Unix domain sockets как локальный IPC между процессами
- [Файловые дескрипторы](../files_and_filesystems/file_descriptors.md) — socket = fd, общие правила работы

## Источники

- `man 2 socket`, `man 2 bind`, `man 2 listen`, `man 2 accept`, `man 2 connect`, `man 2 send`, `man 2 recv`
- `man 7 socket`, `man 7 ip`, `man 7 tcp`, `man 7 udp`, `man 7 unix`
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [The Sockets Networking API (Stevens, Fenner, Rudoff)](https://www.unpbook.com/)
