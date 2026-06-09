# Реализация потоков: системный вызов clone

## Как устроен std::thread на уровне ОС

Стандартный класс `std::thread` в C++ реализован поверх библиотеки **pthreads** (POSIX Threads), которая, в свою
очередь, использует системный вызов `clone()` ядра Linux.

Основные шаги, которые выполняет `std::thread` при создании:

1. Выделяется стек для нового потока (обычно 8 МБ).
2. Вызывается `clone()` с набором флагов, указывающих, какие ресурсы разделять.
3. Новый поток начинает выполнение с указанной функции.
4. TID (thread ID) нового потока сохраняется для последующего управления.
5. `join()` реализуется через `futex` — примитив синхронизации ядра.

Упрощённая концептуальная реализация:

```cpp
class SimpleThread {
    pid_t tid = -1;
    
public:
    template<typename Func>
    SimpleThread(Func f) {
        // Выделяем стек для потока (8MB по умолчанию)
        const size_t stack_size = 8 * 1024 * 1024;
        char* stack = new char[stack_size];
        
        // Функция, которую будет выполнять новый поток
        auto wrapper = [](void* arg) -> int {
            auto* func = static_cast<Func*>(arg);
            (*func)();
            delete func;
            return 0;
        };
        
        // Вызываем clone с флагами для создания потока
        pid_t child_tid;
        tid = clone(
            wrapper,
            stack + stack_size,  // Указатель на верх стека (растёт вниз)
            CLONE_THREAD | CLONE_VM | CLONE_FS | CLONE_FILES |
            CLONE_SIGHAND | CLONE_PARENT_SETTID,
            new Func(f),
            nullptr,
            nullptr,
            &child_tid
        );
        
        if (tid == -1) {
            perror("clone");
        }
    }
    
    void join() {
        // Ждём завершения потока
        if (tid != -1) {
            // Можно использовать futex для ожидания
            // Либо просто ждём сигнала завершения
        }
    }
};
```

## Системный вызов clone

`clone()` — это обобщённый системный вызов для создания новых процессов и потоков. В отличие от `fork()`, он позволяет
точно управлять тем, какие ресурсы разделяются между родительским и дочерним контекстом.

```c
#define _GNU_SOURCE
#include <sched.h>

int clone(int (*fn)(void *arg), void *child_stack,
          int flags, void *arg, pid_t *ptid,
          struct user_desc *tls, pid_t *ctid);
```

### Параметры

- `fn` — функция, которую выполняет новый поток/процесс;
- `child_stack` — указатель на вершину стека дочернего контекста; на x86 стек растёт вниз, поэтому передаётся адрес *
  *конца** выделенного буфера;
- `flags` — битовая маска, определяющая, что разделяется (см. ниже);
- `arg` — аргумент, передаваемый в функцию `fn`;
- `ptid` — если установлен `CLONE_PARENT_SETTID`, по этому адресу записывается TID дочернего;
- `tls` — описатель thread-local storage (TLS);
- `ctid` — если установлен `CLONE_CHILD_CLEARTID`, этот адрес обнуляется при завершении потока (нужно для `futex`
  -ожидания в `pthread_join`).

Возвращаемые значения:

- в вызывающем процессе — TID нового потока/процесса;
- в новом потоке — `0`;
- при ошибке — `-1`.

### Флаги разделения ресурсов

| Флаг                   | Эффект                                                   |
|------------------------|----------------------------------------------------------|
| `CLONE_VM`             | Общее виртуальное адресное пространство                  |
| `CLONE_FS`             | Общие корневая и текущая директория                      |
| `CLONE_FILES`          | Общая таблица файловых дескрипторов                      |
| `CLONE_SIGHAND`        | Общие обработчики сигналов                               |
| `CLONE_THREAD`         | Дочерний помещается в ту же thread-группу                |
| `CLONE_PARENT_SETTID`  | Записать TID дочернего в `ptid`                          |
| `CLONE_CHILD_CLEARTID` | Обнулить `ctid` при завершении потока                    |
| `CLONE_VFORK`          | Родитель блокируется до `exec()` или `exit()` в дочернем |
| `CLONE_NEWNS`          | Новое пространство имён монтирований (для контейнеров)   |
| `CLONE_NEWPID`         | Новое пространство имён PID (для контейнеров)            |

### Что разделяется, а что независимо

Флаги `clone()` определяют, какие ресурсы процесса получит новый поток, а какие останутся его собственными:

```
  Процесс (родитель)
  ┌──────────────────────────────────────────────────────────┐
  │                                                          │
  │  Виртуальное          Таблица FD        CWD / root       │
  │  адресное             (file descriptor  (CLONE_FS)       │
  │  пространство         table)                             │
  │  (CLONE_VM)           (CLONE_FILES)                      │
  │       │                     │                 │          │
  └───────┼─────────────────────┼─────────────────┼──────────┘
          │  clone() с флагами  │                 │
          ▼                     ▼                 ▼
  ┌──────────────────────────────────────────────────────────┐
  │  Новый поток (дочерний task_struct)                      │
  │                                                          │
  │  ОБЩЕЕ (shared):          │  НЕЗАВИСИМОЕ (own):          │
  │  ┌──────────────────────┐ │  ┌──────────────────────┐    │
  │  │ CLONE_VM:            │ │  │ стек (child_stack)   │    │
  │  │   text, data, heap,  │ │  │   отдельный буфер    │    │
  │  │   mmap регионы       │ │  │   (обычно 8 МБ)      │    │
  │  ├──────────────────────┤ │  ├──────────────────────┤    │
  │  │ CLONE_FILES:         │ │  │ регистры CPU         │    │
  │  │   fd 0,1,2,...       │ │  │   rip, rsp, rbp,     │    │
  │  │   один struct files  │ │  │   rax, ...           │    │
  │  ├──────────────────────┤ │  ├──────────────────────┤    │
  │  │ CLONE_FS:            │ │  │ TLS (Thread Local    │    │
  │  │   cwd, root,         │ │  │ Storage)             │    │
  │  │   umask              │ │  │   fs/gs регистры     │    │
  │  ├──────────────────────┤ │  ├──────────────────────┤    │
  │  │ CLONE_SIGHAND:       │ │  │ сигнальная маска     │    │
  │  │   обработчики        │ │  │   (sigprocmask)      │    │
  │  │   сигналов           │ │  ├──────────────────────┤    │
  │  └──────────────────────┘ │  │ pending signals      │    │
  │                           │  │   (per-thread)       │    │
  │                           │  └──────────────────────┘    │
  └──────────────────────────────────────────────────────────┘
```

Без `CLONE_VM` дочерний контекст получает **копию** адресного пространства — это `fork()`. С `CLONE_VM` — оба потока
работают в одном пространстве, изменения в памяти видны немедленно.

## Создание потока через clone

Для создания полноценного потока (аналога того, что делает pthreads) нужна комбинация флагов, обеспечивающая
максимальное разделение ресурсов:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int thread_fn(void *arg) {
    // Это выполняется в контексте нового потока
    int *data = (int *)arg;
    printf("New thread: data = %d\n", *data);
    free(data);
    return 0;
}

int main() {
    // Выделяем стек для потока (обычно 8MB)
    char *stack = malloc(8 * 1024 * 1024);
    if (!stack) {
        perror("malloc");
        return 1;
    }
    
    char *stack_top = stack + (8 * 1024 * 1024);
    
    // Подготавливаем аргумент
    int *arg = malloc(sizeof(int));
    *arg = 42;
    
    // Флаги для создания потока
    int flags = CLONE_THREAD      // Новый поток в той же группе
              | CLONE_VM          // Общее адресное пространство
              | CLONE_FS          // Общая файловая система
              | CLONE_FILES       // Общие FD
              | CLONE_SIGHAND     // Общие обработчики сигналов
              | CLONE_PARENT_SETTID  // Установить tid родителю
              | CLONE_CHILD_CLEARTID; // Очистить tid при выходе
    
    pid_t child_tid;
    pid_t tid = clone(thread_fn, stack_top, flags, arg, &child_tid, NULL, NULL);
    
    if (tid == -1) {
        perror("clone");
        return 1;
    }
    
    printf("Main: created thread with tid = %d\n", tid);
    
    // Для правильной синхронизации обычно используют futex
    // Здесь просто ждём немного
    sleep(1);
    
    free(stack);
    return 0;
}
```

Компиляция:

```bash
gcc -D_GNU_SOURCE clone_thread.c -o clone_thread
./clone_thread
```

## Потоки с точки зрения ОС: thread group

С точки зрения ядра Linux, потоки — это обычные задачи (task), то есть структуры `task_struct`, созданные через
`clone()` с флагами совместного использования ресурсов.

Все потоки одного процесса образуют **thread group** (группу потоков):

- каждый поток имеет уникальный **TID** (Thread ID);
- все потоки группы имеют одинаковый **TGID** (Thread Group ID), равный TID первого потока;
- `getpid()` в userspace возвращает TGID (то есть всегда одно и то же значение для всех потоков процесса);
- первый поток (лидер группы) имеет `TID == TGID`.

Потоки группы видны в `/proc`:

```bash
ls /proc/<pid>/task/
# Выведет директории для каждого TID: 1000/ 1001/ 1002/
```

## Идентификаторы: PID, TID, TGID

| Идентификатор | Значение                                | Возвращает |
|---------------|-----------------------------------------|------------|
| **PID**       | Process ID (для shell и userspace)      | `getpid()` |
| **TID**       | Thread ID (уникален для каждого потока) | `gettid()` |
| **TGID**      | Thread Group ID = PID лидера группы     | `getpid()` |

Пример: процесс с PID=1000 имеет три потока:

| Поток   | TID  | TGID | getpid() |
|---------|------|------|----------|
| Главный | 1000 | 1000 | 1000     |
| Второй  | 1001 | 1000 | 1000     |
| Третий  | 1002 | 1000 | 1000     |

Из кода:

```c
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>

int main() {
    pid_t tid  = gettid();   // TID данного потока
    pid_t tgid = getpid();   // TGID = PID лидера группы
    printf("TID=%d, TGID=%d\n", tid, tgid);
    return 0;
}
```

Через `/proc`:

```bash
cat /proc/self/status | grep -E "^Pid|^Tgid"
# Pid:  1000    <- это TID!
# Tgid: 1000
```

## Отправка сигнала конкретному потоку

Когда сигнал отправляется на весь процесс (`kill(pid, sig)`, где `pid` = TGID), ядро выбирает один из потоков группы для
его обработки. Чтобы доставить сигнал именно нужному потоку, используются специальные системные вызовы:

```c
#include <signal.h>
#include <sys/syscall.h>

// Отправить сигнал по TID (устарел, используйте tgkill)
int tkill(pid_t tid, int sig);

// Предпочтительный вариант: проверяет принадлежность к TGID
int tgkill(pid_t tgid, pid_t tid, int sig);
```

`tgkill` безопаснее `tkill`: он проверяет, что поток с указанным TID действительно принадлежит указанной thread group, и
отклоняет вызов, если поток уже завершился и его TID был переиспользован.

Пример отправки сигнала конкретному потоку из C++:

```cpp
#include <thread>
#include <iostream>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <sys/syscall.h>

std::atomic<pid_t> worker_tid(0);

void signal_handler(int sig) {
    // syscall(SYS_write, ...) — безопасно вызывать из обработчика сигнала
    const char msg[] = "Signal received!\n";
    syscall(SYS_write, 1, msg, sizeof(msg) - 1);
}

void worker() {
    // Сохраняем свой TID, чтобы main мог отправить сигнал
    worker_tid = syscall(SYS_gettid);
    sleep(10);
}

int main() {
    signal(SIGUSR1, signal_handler);

    std::thread t(worker);

    // Ждём, пока поток сохранит свой TID
    while (worker_tid == 0) {
        sched_yield();
    }

    // Отправить SIGUSR1 конкретному потоку
    tgkill(getpid(), worker_tid, SIGUSR1);

    t.join();
    return 0;
}
```

`std::thread` намеренно не предоставляет прямого доступа к TID потока — это деталь реализации. Для получения TID из
потока используется `syscall(SYS_gettid)` или `gettid()` (начиная с glibc 2.30).

## Связанные темы

- [Потоки (основы)](threads_basics.md) — `std::thread` и POSIX pthreads с точки зрения пользователя
- [Синхронизация: мьютексы, семафоры, futex](thread_synchronization.md) — futex как основа мьютексов и `join`
- [fork и exec](../processes/fork_and_exec.md) — `fork()` как частный случай `clone()` без флагов разделения ресурсов
- [Сигналы](../processes/signals.md) — доставка сигналов потокам, `tgkill`

## Источники

- `man 2 clone` — системный вызов clone
- `man 2 gettid` — получение TID
- `man 2 tgkill` — отправка сигнала конкретному потоку
- `man 7 pthreads` — POSIX Threads в Linux
- `man 2 futex` — примитив синхронизации ядра
- `man 5 proc` — файловая система /proc, в том числе /proc/[pid]/task
