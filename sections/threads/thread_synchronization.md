# Синхронизация потоков: мьютексы, семафоры, futex

Когда несколько потоков обращаются к общим данным без координации — возникают **гонки (race conditions)**,
приводящие к непредсказуемым результатам. Для их предотвращения используют примитивы синхронизации.

## Race condition

**Race condition** — ситуация, когда результат программы зависит от непредсказуемого порядка выполнения потоков.

```cpp
int counter = 0;  // общая переменная

void increment() {
    for (int i = 0; i < 100000; ++i)
        counter++;  // НЕ атомарно: read → add → write
}

// 10 потоков → ожидаем 1 000 000, получаем ~600 000
```

`counter++` транслируется в три инструкции. Если между ними произошло переключение контекста,
изменение одного потока перезапишет изменение другого.

## Мьютекс (Mutex)

**Mutex (Mutual Exclusion)** — примитив, гарантирующий, что в **критической секции** находится только один поток.

```cpp
#include <mutex>

int counter = 0;
std::mutex mtx;

void increment() {
    for (int i = 0; i < 100000; ++i) {
        std::lock_guard<std::mutex> lock(mtx);  // RAII: unlock при выходе из scope
        counter++;
    }
}
// Теперь counter == 1 000 000 гарантированно
```

**Pthreads (C API):**

```c
#include <pthread.h>

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_lock(&mtx);
// критическая секция
pthread_mutex_unlock(&mtx);
```

### Deadlock

**Deadlock** — взаимная блокировка: поток A ждёт мьютекс B (который держит поток B),
а поток B ждёт мьютекс A (который держит поток A).

```cpp
std::mutex mtx1, mtx2;

// Поток 1                        // Поток 2
std::lock_guard lock1(mtx1);      std::lock_guard lock2(mtx2);
sleep(1);                         sleep(1);
std::lock_guard lock2(mtx2);  // DEADLOCK  std::lock_guard lock1(mtx1);  // DEADLOCK
```

**Как избежать:**

```cpp
// 1. Всегда захватывать в одном порядке (оба — mtx1, потом mtx2)

// 2. std::lock — атомарный захват нескольких мьютексов
std::lock(mtx1, mtx2);
std::lock_guard l1(mtx1, std::adopt_lock);
std::lock_guard l2(mtx2, std::adopt_lock);

// 3. try_lock с таймаутом
if (mtx1.try_lock_for(std::chrono::milliseconds(100))) { /* ... */ }
```

## Семафор

**Семафор** — целочисленный счётчик с операциями `wait` (P, уменьшить) и `signal` (V, увеличить).

- `wait`: если значение > 0 — уменьшить и продолжить; если 0 — заблокировать.
- `signal`: увеличить и пробудить одного ожидающего.

**Счётный семафор** ограничивает число одновременных потоков (например, пул соединений):

```cpp
#include <semaphore.h>
#include <thread>

sem_t sem;
sem_init(&sem, 0, 3);   // максимум 3 потока одновременно

void worker(int id) {
    sem_wait(&sem);      // P (уменьшить, блокировать при 0)
    // ... работа с ресурсом ...
    sem_post(&sem);      // V (увеличить, пробудить ожидающих)
}
```

**POSIX API:**

```c
sem_init(sem_t *, int pshared, unsigned value);  // pshared=0: только потоки; 1: между процессами
sem_wait(sem_t *);       // блокирующий P
sem_trywait(sem_t *);    // неблокирующий P (-1 + EAGAIN если занято)
sem_post(sem_t *);       // V
sem_getvalue(sem_t *, int *sval);
sem_destroy(sem_t *);
```

**Именованные семафоры** (между процессами):

```c
sem_t *s = sem_open("/my_sem", O_CREAT, 0666, 1);
sem_wait(s);
sem_post(s);
sem_close(s);
sem_unlink("/my_sem");
```

## Futex

**futex (fast userspace mutex)** — низкоуровневый примитив ядра Linux.
Ключевая идея: **без конкуренции** не нужен syscall — только атомарная операция над переменной в памяти.
Syscall делается только при реальной блокировке/пробуждении.

На futex построены `pthread_mutex_t`, `std::mutex`, `sem_t`.

```
futex: fast path vs slow path

  lock() — нет конкуренции (fast path):
  ┌─────────────────────────────────────────────────────────┐
  │  USER SPACE                                             │
  │                                                         │
  │  state == 0  ──▶  CAS(0 → 1)  ──▶  захват               │
  │                    (атомарная CPU-инструкция)           │
  │                    без syscall, без kernel              │
  └─────────────────────────────────────────────────────────┘

  lock() — конкуренция (slow path):
  ┌─────────────────────────────────────────────────────────┐
  │  USER SPACE                                             │
  │                                                         │
  │  state != 0  ──▶  CAS(1 → 2)  ──▶  syscall ─────────┐   │
  └──────────────────────────────────────────────────────┼──┘
                                                         │
  ┌──────────────────────────────────────────────────────┼──┐
  │  KERNEL SPACE                                        │  │
  │                                                      ▼  │
  │         futex wait queue [uaddr]                        │
  │         ┌──────────────────────────┐                    │
  │         │  Thread B  │  Thread C   │  ← спят           │
  │         └──────────────────────────┘                    │
  └─────────────────────────────────────────────────────────┘

  unlock():
  ┌─────────────────────────────────────────────────────────┐
  │  USER SPACE                                             │
  │                                                         │
  │  state.exchange(0)                                      │
  │  old == 2 (были ожидающие)  ──▶  syscall FUTEX_WAKE ─┐  │
  └──────────────────────────────────────────────────────┼──┘
                                                         │
  ┌──────────────────────────────────────────────────────┼──┐
  │  KERNEL SPACE                                        ▼  │
  │         пробудить 1 поток из wait queue                 │
  │         ──▶ Thread B переходит в RUNNABLE               │
  └─────────────────────────────────────────────────────────┘
```

```c
#include <linux/futex.h>
#include <sys/syscall.h>

// Заблокировать поток, если *uaddr == val
syscall(SYS_futex, uaddr, FUTEX_WAIT, val, NULL, NULL, 0);

// Пробудить до n потоков, ожидающих на uaddr
syscall(SYS_futex, uaddr, FUTEX_WAKE, n, NULL, NULL, 0);
```

### Простой mutex на futex

```cpp
#include <atomic>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/futex.h>

class FutexMutex {
    // 0=free, 1=locked, 2=locked+waiters
    std::atomic<uint32_t> state{0};

    static void futex_wait(std::atomic<uint32_t> *a, uint32_t v) {
        syscall(SYS_futex, a, FUTEX_WAIT_PRIVATE, v, nullptr, nullptr, 0);
    }
    static void futex_wake(std::atomic<uint32_t> *a) {
        syscall(SYS_futex, a, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }

public:
    void lock() {
        while (true) {
            uint32_t expected = 0;
            // Быстрый путь: захватить без syscall
            if (state.compare_exchange_strong(expected, 1,
                    std::memory_order_acquire)) return;

            // Медленный путь: есть конкуренция
            expected = 1;
            state.compare_exchange_strong(expected, 2, std::memory_order_relaxed);
            futex_wait(&state, 2);
        }
    }

    void unlock() {
        uint32_t old = state.exchange(0, std::memory_order_release);
        if (old == 2) futex_wake(&state);  // были ожидающие
    }
};
```

| State | Смысл                 | lock()                | unlock()           |
|-------|-----------------------|-----------------------|--------------------|
| 0     | Свободно              | CAS(0→1), без syscall | exchange(0)        |
| 1     | Занято, нет ожидающих | CAS(1→2) + FUTEX_WAIT | exchange(0)        |
| 2     | Занято + ожидающие    | FUTEX_WAIT            | exchange(0) + WAKE |

## Condition variables (условные переменные)

**Condition variable** позволяет потоку ждать наступления условия, атомарно освобождая мьютекс во время ожидания:

```cpp
#include <mutex>
#include <condition_variable>
#include <queue>

std::mutex mtx;
std::condition_variable cv;
std::queue<int> tasks;

void producer() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        tasks.push(42);
    }
    cv.notify_one();  // разбудить одного ожидающего
}

void consumer() {
    std::unique_lock<std::mutex> lock(mtx);
    // Атомарно: освобождает мьютекс и засыпает; захватывает мьютекс при пробуждении
    cv.wait(lock, [] { return !tasks.empty(); });
    int task = tasks.front();
    tasks.pop();
}
```

POSIX API: `pthread_cond_t`, `pthread_cond_wait`, `pthread_cond_signal`, `pthread_cond_broadcast`.

Условие всегда проверяется в цикле: возможны **spurious wakeups** — ложные пробуждения без сигнала.

## Связанные темы

- [Atomic операции и memory model](atomics_and_memory_model.md) — low-level примитивы, на которых построены mutex и
  futex
- [Потоки (основы)](threads_basics.md) — создание потоков, join/detach, TLS
- [Реализация потоков (clone)](thread_implementation_clone.md) — как futex используется для реализации join и мьютексов
  на уровне ядра

## Источники

- `man 2 futex`, `man 3 pthread_mutex_lock`, `man 3 sem_init`
- `man 3 pthread_cond_wait` — условные переменные
- [Futex are tricky — Ulrich Drepper](https://www.akkadia.org/drepper/futex.pdf)
- [cppreference: std::mutex](https://en.cppreference.com/w/cpp/thread/mutex)
- [cppreference: std::condition_variable](https://en.cppreference.com/w/cpp/thread/condition_variable)
