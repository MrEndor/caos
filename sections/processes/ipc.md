# IPC: Pipes, FIFO, Разделяемая память

Межпроцессное взаимодействие (IPC — Inter-Process Communication) в Linux реализуется несколькими механизмами.
В этой статье рассмотрим три «классических» вида IPC на основе файловых абстракций и памяти:
анонимные pipe, именованные FIFO и разделяемую память.

## Сравнение механизмов

| Механизм      | Именован?          | Между любыми процессами?   | Скорость      | Синхронизация         | Передача данных                      |
|---------------|--------------------|----------------------------|---------------|-----------------------|--------------------------------------|
| Pipe          | Нет                | Только родственные (fork)  | Средняя       | Встроена (блокировки) | Поток байт                           |
| FIFO          | Да (файл)          | Любые                      | Средняя       | Встроена              | Поток байт                           |
| Unix socket   | Да (файл/abstract) | Любые                      | Средняя       | Встроена              | Stream / datagram, можно передать FD |
| `socketpair`  | Нет                | Только родственные (fork)  | Средняя       | Встроена              | Двунаправленный stream               |
| eventfd       | Нет (fd)           | Через fork / `pidfd_getfd` | Очень высокая | Встроена в счётчик    | 8-байтный счётчик                    |
| POSIX mqueue  | Да (`/name`)       | Любые                      | Средняя       | Встроена, приоритеты  | Сообщения с приоритетом              |
| Shared memory | Да (`/dev/shm`)    | Любые                      | Высокая       | Нужна явная           | Произвольная структура               |
| System V IPC  | Да (key/id)        | Любые                      | Средняя       | Встроена для msg/sem  | msg / sem / shm                      |

## Анонимные Pipes

**Pipe (конвейер)** — однонаправленный канал связи между двумя процессами.
Данные, записанные в один конец (`fd[1]`), читаются с другого (`fd[0]`).

**Ключевые свойства:**

- буфер в ядре — обычно 64 KB;
- создаётся через `pipe(2)`, наследуется дочерними процессами через `fork(2)`;
- при закрытии всех читателей — запись даёт `SIGPIPE` / `EPIPE`.

```
Pipe: буфер в ядре

  Parent process          Kernel                Child process
  ┌─────────────┐        ┌────────────────────┐  ┌─────────────┐
  │             │        │   pipe buffer      │  │             │
  │  fd[1] ─────┼──▶ write│ ┌──────────────┐  │  │             │
  │  (write end)│        │ │ data data data│──┼──┼──▶ fd[0]    │
  │             │        │ │  (max ~64 KB) │  │  │  (read end) │
  │fd[0]: закрыт│        │ └──────────────┘   │  │fd[1]: закрыт│
  └─────────────┘        └────────────────────┘  └─────────────┘

  Однонаправленный поток: fd[1] ──▶ буфер ──▶ fd[0]
  При пустом буфере:  read() блокируется
  При полном буфере:  write() блокируется
  fd[1] закрыт у всех: read() вернёт 0 (EOF)
  fd[0] закрыт у всех: write() → SIGPIPE / EPIPE
```

```c
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

int main() {
    int fd[2];  // fd[0] = чтение, fd[1] = запись
    pipe(fd);

    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний — читатель
        close(fd[1]);
        char buf[100];
        ssize_t n = read(fd[0], buf, sizeof(buf));
        printf("Child received: %.*s\n", (int)n, buf);
        close(fd[0]);
        return 0;
    }

    // Родитель — писатель
    close(fd[0]);
    const char *msg = "Hello from parent!";
    write(fd[1], msg, strlen(msg));
    close(fd[1]);
    waitpid(pid, NULL, 0);
}
```

### Аналог оператора `|` в bash

```c
int fd[2];
pipe(fd);

pid_t pid1 = fork();
if (pid1 == 0) {
    close(fd[0]);
    dup2(fd[1], STDOUT_FILENO);  // stdout → pipe
    close(fd[1]);
    execlp("echo", "echo", "Hello CAOS", NULL);
}

pid_t pid2 = fork();
if (pid2 == 0) {
    close(fd[1]);
    dup2(fd[0], STDIN_FILENO);   // stdin ← pipe
    close(fd[0]);
    execlp("wc", "wc", "-w", NULL);
}

close(fd[0]); close(fd[1]);
waitpid(pid1, NULL, 0);
waitpid(pid2, NULL, 0);
// Эквивалентно: echo "Hello CAOS" | wc -w
```

### Broken pipe

Ошибка возникает при записи в pipe, у которого нет открытых читателей:

```c
signal(SIGPIPE, SIG_IGN);       // игнорировать сигнал

ssize_t n = write(fd[1], buf, size);
if (n == -1 && errno == EPIPE) {
    // все читатели закрыты
}
```

## FIFO (именованный pipe)

**FIFO** — именованный pipe, видный как специальный файл в ФС (тип `p`).
Позволяет общаться **любым** процессам, не только родственным.

```bash
mkfifo /tmp/my_fifo   # создать из shell
ls -la /tmp/my_fifo   # prw-rw-rw-
```

```c
#include <sys/stat.h>
mkfifo("/tmp/my_fifo", 0666);   // программно
```

**Писатель:**

```c
int fd = open("/tmp/my_fifo", O_WRONLY);
write(fd, "Hello!", 6);
close(fd);
```

**Читатель** (блокируется на `open`, пока нет писателя):

```c
int fd = open("/tmp/my_fifo", O_RDONLY);
char buf[100];
ssize_t n = read(fd, buf, sizeof(buf));
printf("Received: %.*s\n", (int)n, buf);
close(fd);
unlink("/tmp/my_fifo");
```

**Особенности нескольких писателей:**
записи размером ≤ `PIPE_BUF` (обычно 4 KB в Linux) атомарны.
При нескольких читателях каждый байт уходит только одному из них.

## Разделяемая память (Shared Memory)

**Shared memory** — самый быстрый IPC: процессы напрямую обращаются к одному физическому региону.
Не требует копирования данных через ядро, но **требует явной синхронизации**.

```
Shared memory: два процесса видят один физический регион

  Process A                  Physical RAM             Process B
  ┌─────────────────────┐                             ┌──────────────────────┐
  │  virtual address    │                             │  virtual address     │
  │  space              │                             │  space               │
  │  ┌───────────────┐  │    ┌─────────────────┐      │  ┌───────────────┐   │
  │  │  mmap region  │──┼──▶ │  shared memory  │ ◀────┼──│  mmap region  │   │
  │  │  0x7f...      │  │    │  /dev/shm/name  │      │  │  0x7f...      │   │
  │  └───────────────┘  │    │  (физ. страницы)│      │  └───────────────┘   │
  │                     │    └─────────────────┘      │                      │
  │  запись сюда        │ ──────────────────────────▶ │видна здесь немедленно│
  └─────────────────────┘                             └──────────────────────┘

  shm_open() + mmap()  ──▶  MAP_SHARED mapping ──▶  одни и те же физ. страницы
  Синхронизация: mutex / semaphore / atomic — обязательна!
```

### POSIX API (рекомендуется)

```c
#include <sys/mman.h>
#include <fcntl.h>

// Писатель
int fd = shm_open("/my_shm", O_CREAT | O_RDWR, 0666);
ftruncate(fd, 1024);
void *addr = mmap(NULL, 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
strcpy(addr, "Hello!");
munmap(addr, 1024); close(fd);

// Читатель
int fd2 = shm_open("/my_shm", O_RDONLY, 0666);
void *addr2 = mmap(NULL, 1024, PROT_READ, MAP_SHARED, fd2, 0);
printf("%s\n", (char *)addr2);
munmap(addr2, 1024); close(fd2);
shm_unlink("/my_shm");
```

Компиляция: `gcc ... -lrt`

Shared memory объекты живут в `/dev/shm/`.

### System V API (устаревший)

```c
#include <sys/shm.h>
key_t key = ftok("/tmp/shm_file", 'A');
int shmid = shmget(key, 1024, IPC_CREAT | 0666);
void *addr = shmat(shmid, NULL, 0);
// ... работа с addr ...
shmdt(addr);
shmctl(shmid, IPC_RMID, NULL);
```

### Просмотр состояния

```bash
ls -la /dev/shm/        # POSIX shared memory
ipcs -m                 # System V shared memory
cat /proc/<pid>/maps    # все маппинги процесса
pmap <pid>              # удобный вывод
```

## Unix domain sockets (AF_UNIX)

Unix domain sockets — двунаправленный socket-based IPC внутри одного хоста. В отличие от TCP/UDP через `127.0.0.1`,
трафик не идёт через сетевой стек: нет IP-заголовков, нет TCP checksum, нет routing, нет фильтрации netfilter уровня IP.
Ядро просто копирует данные из буфера одного socket'а в буфер другого, что даёт ощутимо более низкие latency и более
высокий throughput, чем loopback TCP.

```
TCP loopback (127.0.0.1)               Unix domain socket
┌──────────────┐                       ┌──────────────┐
│ application  │                       │ application  │
├──────────────┤                       ├──────────────┤
│ socket layer │                       │ socket layer │
├──────────────┤                       ├──────────────┤
│ TCP          │ checksum, seq, ack    │              │
├──────────────┤                       │   AF_UNIX    │
│ IP           │ headers, routing      │   buffer     │  copy_to_buffer()
├──────────────┤                       │              │
│ loopback dev │ qdisc, netfilter      │              │
└──────────────┘                       └──────────────┘
   ~3-5 µs RTT                            ~1-2 µs RTT
```

### SOCK_STREAM vs SOCK_DGRAM

| Свойство          | SOCK_STREAM            | SOCK_DGRAM               |
|-------------------|------------------------|--------------------------|
| Семантика         | Поток байт, без границ | Сообщения с границами    |
| Соединение        | `connect`/`accept`     | Без соединения, `sendto` |
| Гарантия доставки | Внутри хоста — да      | Внутри хоста — да        |
| Порядок           | Сохраняется            | Сохраняется              |
| Сравнимо с        | TCP                    | UDP                      |

`SOCK_SEQPACKET` сочетает оба свойства: соединение + границы сообщений (как SCTP).

### Адресация: pathname vs abstract namespace

Unix sockets бывают трёх видов адресации:

```
┌─────────────────────┬─────────────────────────────────────────────┐
│  pathname           │  sun_path = "/tmp/my.sock"                  │
│  (классический)     │  создаётся inode в ФС, нужно unlink()       │
├─────────────────────┼─────────────────────────────────────────────┤
│  abstract namespace │  sun_path[0] = '\0', далее имя              │
│  (Linux-specific)   │  не появляется в ФС, исчезает с процессом   │
├─────────────────────┼─────────────────────────────────────────────┤
│  unnamed            │  bind не вызывался; ядро присвоит при первом│
│                     │  send (autobind в abstract namespace)       │
└─────────────────────┴─────────────────────────────────────────────┘
```

Abstract socket в `ss -x` отображается как `@name`.

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

struct sockaddr_un addr = {0};
addr.sun_family = AF_UNIX;

// pathname:
strncpy(addr.sun_path, "/tmp/my.sock", sizeof(addr.sun_path) - 1);

// abstract namespace (Linux):
addr.sun_path[0] = '\0';
strncpy(addr.sun_path + 1, "my.sock", sizeof(addr.sun_path) - 2);
socklen_t len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen("my.sock");
```

### socketpair для родственных процессов

`socketpair(2)` создаёт сразу два соединённых socket'а — аналог `pipe(2)`, но двунаправленный и доступен для
`SOCK_STREAM`/`SOCK_DGRAM`/`SOCK_SEQPACKET`. Передаётся в дочерний процесс через `fork()`.

```c
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

    if (fork() == 0) {
        close(sv[0]);
        write(sv[1], "ping", 4);
        char buf[16] = {0};
        read(sv[1], buf, sizeof(buf));
        printf("child got: %s\n", buf);
        return 0;
    }
    close(sv[1]);
    char buf[16] = {0};
    read(sv[0], buf, sizeof(buf));
    printf("parent got: %s\n", buf);
    write(sv[0], "pong", 4);
    wait(NULL);
}
```

### Передача file descriptors через SCM_RIGHTS

Уникальная возможность Unix domain sockets: переслать **открытый file descriptor** от одного процесса другому.
Получающий процесс получает свой собственный FD, указывающий на тот же `struct file` в ядре — refcount увеличивается,
как при `dup`/`fork`. На этом построены protocol activation (systemd), демоны вроде `pulseaudio`, X server passing FDs,
container runtimes.

```
Process A                  kernel                    Process B
┌──────────┐         ┌──────────────────┐         ┌──────────┐
│ fd = 5   │────────▶│ struct file *    │◀────────│ fd = 7   │
└──────────┘         │   refcount: 2    │         └──────────┘
                     └──────────────────┘
        sendmsg(SCM_RIGHTS, fd=5)  ──▶  recvmsg() → fd=7
        (FD номера не совпадают — ядро находит свободный slot у B)
```

Передача через `sendmsg`/`recvmsg` с control message типа `SCM_RIGHTS`:

```c
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

void send_fd(int sock, int fd) {
    struct msghdr msg = {0};
    char buf[CMSG_SPACE(sizeof(int))] = {0};
    char dummy = 'x';
    struct iovec iov = { .iov_base = &dummy, .iov_len = 1 };

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    sendmsg(sock, &msg, 0);
}

int recv_fd(int sock) {
    struct msghdr msg = {0};
    char buf[CMSG_SPACE(sizeof(int))] = {0};
    char dummy;
    struct iovec iov = { .iov_base = &dummy, .iov_len = 1 };

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    recvmsg(sock, &msg, 0);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
    return fd;
}
```

Сопутствующее control message `SCM_CREDENTIALS` (опция сокета `SO_PASSCRED`) даёт получателю PID/UID/GID отправителя —
kernel сам проверяет credentials, обмануть нельзя.

## eventfd: счётчик в ядре для сигнализации

`eventfd(2)` создаёт файловый дескриптор, под которым в ядре живёт 64-битный беззнаковый счётчик. `write()` атомарно
прибавляет значение, `read()` возвращает текущее значение и обнуляет его (или вычитает 1 в режиме `EFD_SEMAPHORE`). Если
счётчик == 0, `read()` блокируется (или возвращает `EAGAIN` в non-blocking режиме).

```
eventfd: 8-байтный счётчик в kernel

  thread A / process A           kernel              thread B / process B
  ┌──────────────────┐                              ┌──────────────────┐
  │ write(efd, &v)   │──▶  counter += v             │                  │
  │ v = 1            │     ┌────────────────┐       │                  │
  └──────────────────┘     │  uint64_t  = N │       │ read(efd, &out)  │
                           └────────────────┘  ──▶  │ out = N          │
                                  │                  │ counter = 0      │
                                  ▼                  └──────────────────┘
                           блокирует read'еров,
                           если counter == 0

  EFD_SEMAPHORE:  read() возвращает 1, counter -= 1 (как семафор)
  EFD_NONBLOCK:   read/write возвращают EAGAIN вместо блокировки
  EFD_CLOEXEC:    закрывается при exec
```

eventfd дешевле pipe для простой сигнализации: один счётчик, один syscall с фиксированными 8 байтами, ядро не выделяет
буфер. Хорошо комбинируется с `epoll`/`poll`/`select` — становится readable, как только counter > 0.

```c
#include <sys/eventfd.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>

int efd;

void *worker(void *arg) {
    sleep(1);
    uint64_t v = 1;
    write(efd, &v, sizeof(v));     // сигнализируем main
    return NULL;
}

int main() {
    efd = eventfd(0, EFD_CLOEXEC);

    pthread_t tid;
    pthread_create(&tid, NULL, worker, NULL);

    uint64_t out;
    read(efd, &out, sizeof(out));   // блокируется до write от worker
    printf("got %lu\n", (unsigned long)out);

    pthread_join(tid, NULL);
    close(efd);
}
```

Типичные применения: wakeup для event loop, готовность данных в lock-free очереди, cross-thread cancellation (один поток
`write(efd, 1)`, остальные ловят через `epoll`).

## System V IPC

System V IPC — три семейства legacy-механизмов из SysV Unix: message queues (`msg*`), semaphores (`sem*`), shared
memory (`shm*`). API получает ключ через `ftok()`, возвращает целочисленный id; объекты живут в ядре до `*ctl(IPC_RMID)`
или перезагрузки.

```c
#include <sys/ipc.h>
#include <sys/shm.h>

key_t key = ftok("/some/file", 'A');
int shmid = shmget(key, 4096, IPC_CREAT | 0666);
void *p = shmat(shmid, NULL, 0);
// ...
shmdt(p);
shmctl(shmid, IPC_RMID, NULL);
```

Просмотр и удаление из shell: `ipcs`, `ipcrm`. Без явного удаления объекты остаются в ядре — типичная утечка ресурсов в
старых сервисах.

### POSIX vs System V

| Аспект                  | POSIX (`mq_*`, `sem_*`, `shm_*`)    | System V (`msgget`, `semget`, `shmget`)            |
|-------------------------|-------------------------------------|----------------------------------------------------|
| Именование              | Строка `/name`                      | `key_t` через `ftok()`                             |
| Тип handle              | File descriptor                     | Целочисленный id                                   |
| Интеграция с poll/epoll | Да (это обычный fd)                 | Нет                                                |
| Жизненный цикл          | Reference counting + `*_unlink`     | Persistent до `IPC_RMID`                           |
| Просмотр в ФС           | `/dev/shm/`, `/dev/mqueue/`         | Только `ipcs`                                      |
| Семафоры                | `sem_post`/`sem_wait`, один счётчик | Массив семафоров, atomic многооперационные `semop` |
| Поддержка               | POSIX.1-2001                        | SUSv2, в Linux считается legacy                    |
| Когда выбирать          | Новый код                           | Совместимость со старыми системами                 |

POSIX API в Linux чище, ложится на event-driven код (fd-based) и не оставляет за собой объекты при падении. System V
встречается в `oracle`, `postgresql` (исторически — shm для shared buffers), некоторых HPC-приложениях.

## Связанные темы

- [Сигналы](signals.md) — сигналы как простейший механизм межпроцессного уведомления (`SIGUSR1`, `SIGPIPE`)
- [mmap и маппинг файлов](../memory/mmap_and_file_mapping.md) — основа для POSIX shared memory
- [fork и exec](fork_and_exec.md) — наследование дескрипторов pipe через fork

## Источники

- `man 2 pipe`, `man 7 pipe`
- `man 3 mkfifo`, `man 7 fifo`
- `man 3 shm_open`, `man 2 mmap`, `man 7 shm_overview`
- `man 7 mq_overview` — POSIX message queues
- `man 7 unix` — UNIX domain sockets, abstract namespace, SCM_RIGHTS
- `man 2 socketpair`, `man 3 cmsg`
- `man 2 eventfd`, `man 7 eventfd`
- `man 7 sysvipc`, `man 2 shmget`, `man 2 msgget`, `man 2 semget`
- [Linux kernel: pipe(7)](https://man7.org/linux/man-pages/man7/pipe.7.html)
- [Beej's Guide to Unix IPC](https://beej.us/guide/bgipc/)
