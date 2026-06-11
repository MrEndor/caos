# CAOS examples — рабочие сниппеты к 21 вопросу

Полноценные компилируемые примеры к экзаменационным темам CAOS. Все файлы — на C++20, компилируются без warning'ов (`-Wall -Wextra`).

Три типа примеров:

- **demo** — иллюстративные программы, которые запускаются и печатают вывод (серверы, фибры, callback hell);
- **test** — проверка корректности через **GoogleTest** (`*_test.cpp`);
- **bench** — замер времени/throughput через **GoogleBenchmark** (`*_bench.cpp`).

## Сборка и запуск

```bash
make            # собрать всё (demo + tests + benches) в bin/
make demos      # только иллюстративные
make test       # собрать и ЗАПУСТИТЬ все GoogleTest
make bench      # собрать и ЗАПУСТИТЬ все GoogleBenchmark
make q05        # один пример по номеру вопроса
make clean
```

Требования: `g++ >= 11` (C++20), `liburing-dev` (q12), **GoogleTest**, **GoogleBenchmark**, x86-64 Linux (futex/epoll/io_uring; q15_asm — ассемблер System V).

Серверы (q01, q02, q10, q11, q12) слушают `127.0.0.1:8080`; клиент шлёт строку из argv:

```bash
./bin/q11_server &
./bin/q02_client "hello"    # → echo (5 байт): hello
kill %1
```

## Соответствие вопросам

| № | Каталог | Тип | Тема | Статья wiki |
|---|---------|-----|------|-------------|
| 1 | `q01_sync_tcp_server/` | demo | socket/bind/listen/accept, синхронный сервер | [tcp_servers](../sections/concurrency/tcp_servers.md), [sockets_basics](../sections/networking/sockets_basics.md) |
| 2 | `q02_threaded_tcp/` | demo | thread-per-connection сервер + клиент | [tcp_servers](../sections/concurrency/tcp_servers.md) |
| 3 | `q03_cpp_mutexes/` | test | mutex, shared/recursive, unique/shared_lock, std::lock | [sync_primitives_cpp](../sections/concurrency/sync_primitives_cpp.md) |
| 4 | `q04_condvar_semaphore/` | test | condition_variable, counting_semaphore, семафор через cv | [sync_primitives_cpp](../sections/concurrency/sync_primitives_cpp.md) |
| 5 | `q05_thread_pool/` | test + bench | thread pool с блокирующей очередью | [thread_pool](../sections/concurrency/thread_pool.md) |
| 6 | `q06_atomic_spinlock/` | bench | data race, std::atomic, спинлок (TAS/TTAS), mutex | [atomics_and_memory_model](../sections/concurrency/atomics_and_memory_model.md) |
| 7 | `q07_futex_mutex/` | test | futex, mutex через futex | [thread_synchronization](../sections/concurrency/thread_synchronization.md) |
| 8 | `q08_three_state_mutex/` | test | мьютекс на трёх состояниях | [thread_synchronization](../sections/concurrency/thread_synchronization.md) |
| 9 | `q09_condvar_futex/` | test | condition_variable через futex, счётчик поколений | [thread_synchronization](../sections/concurrency/thread_synchronization.md) |
| 10 | `q10_nonblocking_busyloop/` | demo | неблокирующие read/write, busy-loop polling | [tcp_servers](../sections/concurrency/tcp_servers.md) |
| 11 | `q11_epoll_server/` | demo | select / poll / epoll — три echo-сервера для сравнения API (`q11_select`, `q11_poll`, `q11_epoll`) | [io_multiplexing](../sections/networking/io_multiplexing.md) |
| 12 | `q12_io_uring_server/` | demo | io_uring, сервер через io_uring | [io_uring](../sections/networking/io_uring.md) |
| 13 | `q13_future_promise/` | demo | Future/Promise, callback hell, chaining | [future_promise](../sections/concurrency/future_promise.md) |
| 14 | `q14_future_impl/` | test | реализация std::future/std::promise | [future_promise](../sections/concurrency/future_promise.md) |
| 15 | `q15_fibers/` | demo | файберы (ucontext + свой asm context switch), scheduler, fault injection | [fibers](../sections/concurrency/fibers.md) |
| 16 | `q16_coroutines/` | test | stackful/stackless, co_await, co_return | [coroutines_cpp20](../sections/concurrency/coroutines_cpp20.md) |
| 17 | `q17_false_sharing/` | bench | MESI, cache ping-pong, false sharing | [cpu_caches](../sections/assembler_and_processor/cpu_caches.md) |
| 18 | `q18_treiber_stack/` | test | lock-free/wait-free, Treiber stack | [lock_free_structures](../sections/concurrency/lock_free_structures.md) |
| 19 | `q19_ms_queue/` | test | Michael-Scott queue, ABA | [lock_free_structures](../sections/concurrency/lock_free_structures.md) |
| 20 | `q20_atomic_shared_ptr/` | test | reclamation, atomic_shared_ptr, hazard pointers | [lock_free_structures](../sections/concurrency/lock_free_structures.md) |
| 21 | `q21_memory_model/` | test | модель памяти, store buffering, барьеры, mfence | [atomics_and_memory_model](../sections/concurrency/atomics_and_memory_model.md) |

## Фибры без ucontext

q15 содержит две реализации: `fiber_ucontext.cpp` (POSIX ucontext) и `fiber_asm.cpp` + `context_switch.S` — собственный context switch на ассемблере x86-64 (save/restore callee-saved + rsp, trampoline для первого запуска). Обе чередуют фибры одинаково; asm-версия не делает syscall на маску сигналов.

## Что показывают бенчи (типовой прогон)

- **q06** — под contention TAS-спинлок деградирует до ~12M ops/s @8 потоков, TTAS держит ~28M (TTAS наглядно лучше TAS); `std::atomic` и `std::mutex` для сравнения.
- **q17** — false sharing ~77M ops/s vs padded (`alignas(64)`) ~965M ops/s @4 потока — разница ~12× из-за cache ping-pong (MESI invalidate).
