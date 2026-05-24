# CAOS Wiki — Низкоуровневое программирование

Материалы курса по архитектуре компьютеров и операционным системам (АКОС / CAOS).

## Разделы

| Раздел | Темы |
|--------|------|
| [1. Линковка и библиотеки](linking_and_libraries/index.md) | Стадии сборки, ELF, символы, статическая/динамическая линковка |
| [2. Файлы и файловые системы](files_and_filesystems/index.md) | Файловые дескрипторы, VFS, inode, права доступа |
| [3. Память](memory/index.md) | Виртуальная память, mmap, malloc/free, защита, huge pages |
| [4. Процессы](processes/index.md) | fork/exec, состояния, сигналы, IPC, context switch |
| [5. Потоки и concurrency](threads/index.md) | pthread, futex, atomics, memory ordering, fiber'ы, coroutines |
| [6. Изоляция и контейнеры](isolation/index.md) | namespaces, cgroups, seccomp — фундамент Docker и LXC |
| [7. Ассемблер и процессор](assembler_and_processor/index.md) | x86-64 ASM, кэши, SIMD, прерывания, syscall, vDSO |
| [8. Сети](networking/index.md) | Сокеты, TCP/UDP, epoll, io_uring |
| [9. Инструменты и отладка](tools_and_debugging/index.md) | GDB, sanitizers, perf, strace, valgrind |

## Формат страниц

Каждая страница — энциклопедическая статья с примерами кода, таблицами и разделом **Источники**.
Примеры кода копируются кнопкой в правом верхнем углу блока.
