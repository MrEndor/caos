# Потоки и concurrency

Потоки — это единицы исполнения внутри процесса: разделяют адресное пространство и файловые дескрипторы, но имеют
собственные стеки, регистры и TLS. Этот раздел рассматривает создание и синхронизацию потоков, низкоуровневые
atomic-операции и memory model, а также userspace-аналог переключения контекста — fiber'ы и coroutine'ы через `setjmp`/
`longjmp` и `ucontext`.

## Темы

| Страница                                                              | Что рассматривается                                                                                               |
|-----------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| [Потоки (основы)](threads_basics.md)                                  | `std::thread`, join/detach, RAII, TLS (`__thread`, `pthread_key_create`), модели TLS, `std::jthread` (C++20)      |
| [Реализация потоков (clone)](thread_implementation_clone.md)          | `clone(2)`, флаги `CLONE_*`, thread group, TID/TGID, `tgkill`                                                     |
| [Синхронизация: мьютексы, семафоры, futex](thread_synchronization.md) | Race conditions, deadlock, `std::mutex`, POSIX semaphores, устройство futex, spurious wakeups                     |
| [Atomic операции и memory model](atomics_and_memory_model.md)         | `LOCK` prefix, C11/C++11 atomics, memory ordering (relaxed/acquire/release/seq_cst), CAS, ABA, lock-free паттерны |
| [setjmp/longjmp и ucontext](userspace_context_switching.md)           | jmp_buf layout, реализация в glibc, sigsetjmp, ucontext, fiber'ы, stackful vs stackless coroutines                |
