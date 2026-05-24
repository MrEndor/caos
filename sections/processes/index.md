# Процессы

Процесс — это изолированный контейнер для исполнения программы со своим адресным пространством, таблицей файловых
дескрипторов и правами доступа. Этот раздел рассматривает жизненный цикл процессов в Linux: создание (`fork`, `exec`),
завершение и сбор статуса (`wait`, зомби), управление (приоритеты, CPU affinity, capabilities, rlimit), асинхронные
уведомления (`signals`), межпроцессное взаимодействие (`IPC`) и низкоуровневое переключение контекста между задачами.

## Темы

| Страница                                                                          | Что рассматривается                                                                                    |
|-----------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------|
| [Основы процессов](processes_basics.md)                                           | PID, PPID, дерево процессов, мониторинг через `ps`/`top`/`/proc`, UID/EUID                             |
| [fork и exec](fork_and_exec.md)                                                   | Создание процессов через `fork`, Copy-on-Write, замена образа через `exec*`, идиома fork+exec          |
| [Состояния процессов, wait, sleep](process_states_wait_sleep.md)                  | Диаграмма состояний (R/S/D/T/Z), зомби и orphan-процессы, `wait`/`waitpid`                             |
| [Приоритеты, аффинность, capabilities](process_priority_affinity_capabilities.md) | Nice-приоритеты, RT-планировщики, CPU affinity (`taskset`), Linux capabilities, cgroups CPU controller |
| [rlimit](rlimit.md)                                                               | Ограничение ресурсов процесса: CPU-время, память, файловые дескрипторы, стек; `setrlimit`/`ulimit`     |
| [Сигналы](signals.md)                                                             | Доставка и обработка сигналов, `sigaction`, маска, realtime signals, sigaltstack, signalfd             |
| [IPC: Pipes, FIFO, Shared memory](ipc.md)                                         | Анонимные pipe, FIFO, POSIX shared memory, Unix domain sockets, eventfd, System V IPC                  |
| [Context switch (kernel)](context_switch.md)                                      | `__switch_to`, switch_mm, PCID/ASID, XSAVE, eager vs lazy FPU, preemption models, цена ~1-5 µs         |
