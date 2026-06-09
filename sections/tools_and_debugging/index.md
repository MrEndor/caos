# Инструменты и отладка

Раздел про инструментарий для анализа поведения программ: дебаггеры, sanitizers, профилировщики, трассировка системных
вызовов. Здесь не про язык программы и не про конкретный kernel-механизм, а про то, как **посмотреть и измерить** то,
что уже работает (или не работает).

## Темы

| Страница                                                | Что рассматривается                                                                                                                                                           |
|---------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| [Отладка (GDB, sanitizers, perf, strace)](debugging.md) | `gdb` (breakpoints, watchpoints, core dumps), AddressSanitizer / UBSan / TSan / MSan, `perf stat`/`record`/`sched`, `strace`/`ltrace`, valgrind callgrind/cachegrind/helgrind |
| [ptrace: process tracing изнутри](ptrace.md)            | `PTRACE_ATTACH`/`SEIZE`, syscall stops, code injection, Yama LSM, реализация strace в 50 строк, graftcp как нестандартное применение                                          |

eBPF вынесен в [отдельный раздел](../ebpf/index.md): foundations, tracing, networking, security.
