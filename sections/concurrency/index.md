# Concurrency

Многопоточность и асинхронность — основа любого современного backend'а. Этот раздел покрывает полный спектр: от создания pthread'а через `clone(2)` до lock-free структур и C++20 coroutine'ов; от классических примитивов синхронизации (`mutex`, `semaphore`, `condition_variable`) до эволюции серверных моделей (синхронный → thread pool → epoll → io_uring).

Раздел построен по программе курса CAOS — каждая тема разобрана с механикой «под капотом»: как mutex реализован через futex, как `std::shared_ptr` делает atomic refcount, как scheduler фибр прячет stack switching за `yield()`.

## Темы

| Страница                                                              | Что рассматривается                                                                                                                                                                |
|-----------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [Потоки (основы)](threads_basics.md)                                  | `std::thread`, join/detach, RAII, TLS (`__thread`, `pthread_key_create`), модели TLS, `std::jthread` (C++20)                                                                       |
| [Реализация потоков (clone)](thread_implementation_clone.md)          | `clone(2)`, флаги `CLONE_*`, thread group, TID/TGID, `tgkill`                                                                                                                      |
| [Синхронизация: мьютексы, семафоры, futex](thread_synchronization.md) | Race conditions, deadlock, `std::mutex`, POSIX semaphores, устройство futex, 3-state mutex (OneShot), cv через futex со счётчиком поколений, spurious wakeups                      |
| [C++ примитивы синхронизации](sync_primitives_cpp.md)                 | `shared_mutex`, `recursive_mutex`, `unique_lock`, `shared_lock`, `scoped_lock`, `std::lock`, `counting_semaphore`, семафор через cv                                                |
| [Thread pool](thread_pool.md)                                         | Blocking queue через cv, реализация ~60 строк, submit с future через `packaged_task`, work-stealing, sizing (CPU/IO-bound, Little's law)                                           |
| [Atomic операции и memory model](atomics_and_memory_model.md)         | `LOCK` prefix, C11/C++11 atomics, memory ordering (relaxed/acquire/release/seq_cst), CAS, ABA, spinlock, SPSC queue, store buffering на x86 TSO, Dekker's algorithm                |
| [Lock-free структуры](lock_free_structures.md)                        | Progress guarantees (lock-free vs wait-free), Treiber stack, Michael-Scott queue, hazard pointers, RCU, epoch-based reclamation, `std::atomic_shared_ptr`                          |
| [TCP servers: эволюция моделей](tcp_servers.md)                       | Sync single-thread → thread-per-connection → thread pool → non-blocking + busy-loop → segue к epoll/io_uring, сравнение моделей                                                    |
| [Future и Promise](future_promise.md)                                 | Callback hell vs continuations, `std::future`/`std::promise`, `std::async`, `std::packaged_task`, shared state implementation, `.then`/when_all/when_any, folly/concurrencpp/P2300 |
| [setjmp/longjmp и ucontext](userspace_context_switching.md)           | jmp_buf layout, реализация в glibc, sigsetjmp, ucontext, scheduler-based fiber, fault injection (Loom/Shuttle), stackful vs stackless coroutines, C++20 co_await                   |
| [Fibers: stackful coroutines](fibers.md)                              | Boost.Context jump_fcontext (asm x86-64), make_fcontext, scheduler, work-stealing deque, M:N модель, guard pages, Go/Tokio/Loom                                                    |
| [C++20 coroutines (stackless)](coroutines_cpp20.md)                   | Compiler transformation в state machine, promise_type, awaitable/awaiter, Generator/Task реализация, symmetric transfer, HALO, cppcoro/folly::coro/stdexec                         |
