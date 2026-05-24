# Отладка программ

## Сборка в режиме отладки

По умолчанию `g++` генерирует бинарь без отладочной информации. Для полноценной работы с отладчиком нужно скомпилировать
с двумя флагами:

- `-g` (или `-g3` для максимального объёма) — добавляет таблицы DWARF: соответствие адресов строкам исходника, имена
  переменных, типы, макросы;
- `-O0` (или `-Og`) — отключает или снижает оптимизации.

```bash
g++ -g -O0 main.cpp -o main
```

Флаг `-g` добавляет в ELF-бинарь секции `.debug_info`, `.debug_line`, `.debug_abbrev` и другие — они используются `gdb`
для отображения исходного кода. Подробнее о том, как задаётся флаг `-g` на стадии компиляции — в
статье [Стадии сборки](../linking_and_libraries/build_stages.md).

Отличия debug-сборки от release:

| Свойство            | Debug     | Release                |
|---------------------|-----------|------------------------|
| Размер бинаря       | Крупнее   | Меньше                 |
| Скорость            | Медленнее | Быстрее                |
| Исходный код в gdb  | Да        | Нет                    |
| Значения переменных | Да        | Часто «оптимизировано» |

## Основы работы с gdb

### Запуск

```bash
gdb ./main
gdb --args ./main arg1 arg2
```

Или передать аргументы уже внутри gdb:

```
(gdb) run arg1 arg2
```

### Точки останова (breakpoints)

```
(gdb) break main          # по имени функции
(gdb) break file.cpp:42   # по файлу и строке
(gdb) info breakpoints    # список всех breakpoint’ов
(gdb) delete 1            # удалить breakpoint #1
```

### Пошаговое выполнение

```
(gdb) run           # запустить
(gdb) next          # (n) следующая строка, без захода в функцию
(gdb) step          # (s) следующая строка, с заходом в функцию
(gdb) continue      # (c) продолжить до следующего breakpoint’а
(gdb) finish        # выполнить до конца текущей функции
```

### Просмотр переменных

```
(gdb) print x             # значение переменной x
(gdb) print *ptr          # разыменовать указатель
(gdb) info locals         # все локальные переменные текущего фрейма
(gdb) info args           # аргументы текущей функции
(gdb) display x           # автоматически печатать x после каждого шага
```

## Стек вызовов

```
(gdb) backtrace      # (bt) вывести стек вызовов
(gdb) bt full        # стек + локальные переменные в каждом фрейме
(gdb) frame 2        # переключиться на фрейм #2
(gdb) up             # перейти на фрейм выше
(gdb) down           # перейти на фрейм ниже
```

## Core dump: анализ упавшей программы

Когда процесс завершается из-за фатального сигнала (`SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL` и др.), ядро может
сохранить **дамп памяти** — снимок адресного пространства процесса в момент падения:

```
Segmentation fault (core dumped)
```

Чтобы core dump создавался, нужно снять лимит на его размер:

```bash
ulimit -c unlimited
```

После падения появится файл `core` или `core.<pid>`. Анализ:

```bash
gdb ./main core
```

Внутри gdb core dump выглядит как программа, остановленная в момент краша:

```
(gdb) bt               # стек вызовов в момент падения
(gdb) frame 2          # перейти к нужному фрейму
(gdb) info locals      # локальные переменные в этом фрейме
(gdb) print errno      # посмотреть errno
```

Так можно расследовать падение без повторного воспроизведения.

## Watchpoints: наблюдение за памятью

`gdb` позволяет отслеживать чтение или запись конкретного адреса памяти:

```
(gdb) watch x            # остановиться при изменении x
(gdb) rwatch x           # остановиться при чтении x
(gdb) awatch x           # остановиться при чтении или записи x
(gdb) info watchpoints   # список watchpoints
```

Watchpoints работают через аппаратные регистры отладки (обычно не более 4 штук) и практически не замедляют программу.

## Условные точки останова

```
(gdb) break main.cpp:42 if x > 10    # остановиться только если x > 10
(gdb) condition 1 x > 10             # добавить условие к breakpoint #1
```

## Отладка многопоточных программ

```
(gdb) info threads          # список потоков
(gdb) thread 2              # переключиться на поток #2
(gdb) thread apply all bt   # backtrace для всех потоков сразу
set scheduler-locking on    # заморозить остальные потоки при step/next
```

## Sanitizers

Sanitizers — это инструменты от Google (изначально из проекта Chromium), встроенные в `gcc` и `clang`. Они
инструментируют код на этапе компиляции: вставляют проверки перед каждым доступом к памяти, перед каждой загрузкой
неинициализированного значения, перед каждым обращением к разделяемой переменной. Найденная ошибка печатается в `stderr`
со стеком вызовов и часто с дополнительным контекстом (какой malloc выделил блок, какой free его освободил).

В отличие от `valgrind`, sanitizer'ы не эмулируют CPU — они компилируются в сам бинарь. Это даёт в 5-20 раз меньшие
накладные расходы, но требует пересборки.

### AddressSanitizer (ASan)

Ловит классические ошибки доступа к памяти:

- **heap-buffer-overflow** — выход за границы блока, выделенного через `malloc`/`new`;
- **stack-buffer-overflow** — выход за границы локального массива;
- **global-buffer-overflow** — выход за границы глобального массива;
- **use-after-free (UAF)** — обращение к освобождённой памяти;
- **use-after-return** — обращение к локальной переменной после возврата из функции (требует
  `ASAN_OPTIONS=detect_stack_use_after_return=1`);
- **double-free**, **invalid free** — освобождение того, что не выделялось malloc;
- **memory leaks** — забытые `free`/`delete` (LeakSanitizer встроен в ASan).

Как работает: вокруг каждого выделенного блока ставится **red zone** — буферная область, помеченная как «отравленная» (
`poisoned`). Параллельно ведётся **shadow memory** — байт shadow на каждые 8 байт пользовательской памяти, кодирующий
доступность. Перед каждым load/store компилятор вставляет проверку shadow.

```
Память программы                  Shadow memory (1 байт на 8 байт)
┌───────────────────────────┐    ┌────┐
│ red zone (poisoned)       │    │ FA │  ← FA = heap left redzone
├───────────────────────────┤    ├────┤
│ user data (32 байта)      │    │ 00 │  ← 00 = accessible
│                           │    │ 00 │
│                           │    │ 00 │
│                           │    │ 00 │
├───────────────────────────┤    ├────┤
│ red zone (poisoned)       │    │ FA │  ← FA = heap right redzone
└───────────────────────────┘    └────┘
                                   ▲
                                   └── любой доступ сюда → ASan report
```

```bash
gcc -fsanitize=address -fno-omit-frame-pointer -g prog.c -o prog
./prog
```

Накладные расходы: ~2x по времени, ~2-3x по памяти. Несовместим с `MemorySanitizer` и `ThreadSanitizer` в одной сборке.

### UndefinedBehaviorSanitizer (UBSan)

Ловит undefined behavior из стандарта C/C++:

- знаковое переполнение (`INT_MAX + 1`);
- сдвиг на величину ≥ ширины типа (`1 << 32` для `int`);
- разыменование `NULL` и misaligned pointer;
- деление на ноль;
- выход за границы массива при известном размере;
- вызов функции через указатель неверного типа;
- использование значения после `static_cast` к неподходящему типу.

```bash
gcc -fsanitize=undefined -g prog.c -o prog
# Подключить отдельные проверки:
gcc -fsanitize=signed-integer-overflow,null,alignment -g prog.c -o prog
```

Накладные расходы минимальные — порядка 10-20%. UBSan можно включать вместе с ASan: `-fsanitize=address,undefined`.

### ThreadSanitizer (TSan)

Обнаруживает **data races** — одновременный доступ к одной переменной из разных потоков без синхронизации, где хотя бы
один доступ — запись. Также ловит deadlock'и (через aux-режим `detect_deadlocks`).

Работает по алгоритму **happens-before**: TSan инструментирует каждый load/store и каждую операцию синхронизации (mutex,
atomic, барьер), строит граф зависимостей и проверяет, упорядочены ли два конфликтующих доступа.

```bash
gcc -fsanitize=thread -fPIE -pie -g prog.c -o prog
```

Накладные расходы: ~5-15x по времени, ~5-10x по памяти. Несовместим с ASan. Требует пересборки **всех** библиотек,
включая стандартную (иначе race в `libstdc++` не виден).

### MemorySanitizer (MSan)

Ловит чтение неинициализированной памяти. Каждому биту памяти соответствует shadow-бит «определён/не определён». Запись
делает биты определёнными, чтение копирует shadow. Reports выдаётся только когда неопределённое значение влияет на
наблюдаемое поведение (branch, syscall argument, output).

```bash
clang -fsanitize=memory -fno-omit-frame-pointer -g prog.c -o prog
```

Только в `clang` (в `gcc` не поддерживается). Накладные расходы: ~3x. Требует пересборки **всех** зависимостей с MSan,
включая libc — на практике используется в проектах с собственной сборкой stdlib или через `libc++` + `msan`
-инструментированную libc.

### Сравнительная таблица

| Sanitizer | Что ловит                    | Накладные расходы   | Компилятор   | Совместим с      |
|-----------|------------------------------|---------------------|--------------|------------------|
| ASan      | OOB, UAF, double-free, leaks | ~2x время, 2-3x RAM | gcc, clang   | UBSan            |
| UBSan     | UB по стандарту C/C++        | ~10-20%             | gcc, clang   | ASan, TSan, MSan |
| TSan      | data races, deadlocks        | 5-15x время         | gcc, clang   | UBSan            |
| MSan      | use of uninitialized memory  | ~3x время           | только clang | UBSan            |

Когда использовать что: ASan — в CI на каждый PR (находит большинство багов памяти), UBSan — всегда вместе с ASan,
TSan — для многопоточных компонентов отдельным прогоном, MSan — нишево, требует собственной сборки stdlib.

## perf

`perf` — Linux-нативный профайлер, использующий **PMU (Performance Monitoring Unit)** — аппаратные счётчики событий
внутри CPU. PMU считает события (instructions retired, cache misses, branch mispredictions) без программного оверхеда —
счётчик инкрементируется в железе. Часть этой инфраструктуры в ядре называется `perf_events`.

### perf stat

Снимает агрегированные счётчики PMU за время работы программы:

```bash
perf stat ./prog
perf stat -e cycles,instructions,cache-misses,branch-misses ./prog
perf stat -d ./prog                # detailed: добавляет L1/LLC cache
perf stat -p <pid>                 # уже запущенный процесс
perf stat -a sleep 5               # система целиком за 5 секунд
```

Типичный вывод:

```
   1 234 567      cycles
   2 345 678      instructions       #   1.90  insn per cycle
      12 345      cache-misses       #  15.2 % of all cache refs
       5 678      branch-misses      #   0.45 % of all branches
```

`insn per cycle` (IPC) — главный индикатор: больше 1.0 — CPU хорошо загружен; меньше 0.5 — что-то его тормозит (memory
stalls, branch mispredictions).

### perf record и perf report

Сэмплирующее профилирование: ядро каждые N циклов прерывает программу и записывает текущий стек в файл `perf.data`.
Затем `perf report` показывает, в каких функциях программа провела больше всего времени:

```bash
perf record -g ./prog              # -g включает запись стеков (call graph)
perf record -F 999 -g ./prog       # 999 Hz — сэмпл-частота
perf report                        # интерактивный TUI
perf report --stdio --sort=overhead,comm,dso | head
```

```
events  ▼
┌──────────────────────────┐
│ kernel │ syscall, sched  │
│        │ перерыв         │  ← каждые ~1ms прерывание
│   ↓    │                 │
│ process│  RIP, RSP       │  ← снимок IP и стека
│        │  стек 32 фрейма │
│   ↓    │                 │
│  диск  │  perf.data      │
└──────────────────────────┘
                            
perf report:
  35.2%  app  matrix_multiply
  18.7%  app  hash_lookup
   9.1%  libc memcpy
   ...
```

### Flamegraphs

Brendan Gregg придумал **flamegraph** — визуализацию профиля как стека прямоугольников: ширина = доля времени, высота =
глубина стека. Top функции на вершине — это «hot spots».

```bash
perf record -F 99 -g ./prog
perf script | stackcollapse-perf.pl | flamegraph.pl > prof.svg
```

Скрипты `stackcollapse-perf.pl` и `flamegraph.pl` берут из репозитория `brendangregg/FlameGraph` на GitHub.

## strace и ltrace

`strace` трассирует **системные вызовы** процесса через ptrace (или, в новых ядрах, через `seccomp-bpf` с
`--seccomp-bpf` для меньшего оверхеда). `ltrace` — то же самое для вызовов **функций библиотек** (через перехват PLT).

```bash
strace ./prog                          # все syscall
strace -e trace=openat,read,write ./prog
strace -e trace=network ./prog          # только сетевые
strace -e trace=%file ./prog            # все, работающие с файлами
strace -f ./prog                        # -f: следить и за fork'нутыми
strace -c ./prog                        # -c: только сводка по syscall
strace -p <pid>                         # уже запущенный процесс
strace -o trace.log ./prog              # вывод в файл
ltrace ./prog                           # вызовы libc и других .so
ltrace -e malloc+free ./prog            # только malloc/free
```

Сводка `strace -c`:

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 45.12    0.012345         123       100           read
 23.45    0.006789          67       101           write
 ...
```

Накладные расходы strace через ptrace — каждый syscall удваивается (вход/выход из ядра дважды). Для production
использовать `bpftrace` или `perf trace`.

## valgrind beyond memcheck

`valgrind` — это framework для динамической инструментации; `memcheck` — лишь один из инструментов. Все они работают
через JIT-перекомпиляцию бинаря в промежуточное представление и обратно. Замедление 10-50x.

| Инструмент | Что делает                                                                             |
|------------|----------------------------------------------------------------------------------------|
| memcheck   | OOB, UAF, утечки, неинициализированные чтения (аналог ASan + MSan, но медленнее)       |
| callgrind  | call graph + точное число выполненных инструкций на каждую функцию (для `kcachegrind`) |
| cachegrind | симулирует L1/LL cache и branch predictor, показывает cache miss rate по строкам кода  |
| helgrind   | детектор data race для pthreads (аналог TSan)                                          |
| massif     | heap profiler — график потребления heap во времени, кто аллоцирует                     |

```bash
valgrind --tool=callgrind ./prog        # → callgrind.out.<pid>
kcachegrind callgrind.out.12345         # GUI-визуализация
valgrind --tool=cachegrind ./prog
cg_annotate cachegrind.out.<pid>        # построчная аннотация
valgrind --tool=helgrind ./prog         # ищет data races
valgrind --tool=massif ./prog
ms_print massif.out.<pid>
```

Когда выбирать valgrind, а когда sanitizer'ы: sanitizer'ы быстрее и точнее на UAF/OOB, но требуют пересборки. `valgrind`
работает с production-бинарём без перекомпиляции и даёт `callgrind`/`cachegrind`, которых у sanitizer'ов нет.

## Связанные темы

- [Стадии сборки](../linking_and_libraries/build_stages.md) — флаг `-g` и его влияние на бинарь
- [Символы и манглирование](../linking_and_libraries/symbols_and_mangling.md) — что такое таблица символов и почему
  `strip` мешает отладке
- [Системные вызовы: введение](../assembler_and_processor/system_calls_introduction.md) — `strace` как альтернатива
  `gdb` для трассировки взаимодействия с ОС
- [malloc и free: как это работает внутри](../memory/malloc_and_free_implementation.md) — что находит ASan в heap
- [Защита памяти](../memory/memory_protection.md) — ASLR при отладке (`setarch -R`)

## Источники

- `man gdb` — документация GNU Debugger
- `man 5 core` — формат core dump файла
- `man ulimit` / `man bash` (раздел BUILTINS) — управление лимитами
- `man strace`, `man ltrace`, `man perf`, `man valgrind`
- [Debugging with GDB](https://sourceware.org/gdb/current/onlinedocs/gdb/) — полное руководство пользователя gdb
- [AddressSanitizer wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [ThreadSanitizer wiki](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual)
- [MemorySanitizer wiki](https://github.com/google/sanitizers/wiki/MemorySanitizer)
- [UBSan documentation — clang](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- [Brendan Gregg: Linux perf examples](https://www.brendangregg.com/perf.html)
- [Brendan Gregg: Flame Graphs](https://www.brendangregg.com/flamegraphs.html)
- [FlameGraph repository](https://github.com/brendangregg/FlameGraph)
- [Valgrind tool suite](https://valgrind.org/docs/manual/manual.html)
