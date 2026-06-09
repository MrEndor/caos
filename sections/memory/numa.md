# NUMA: Non-Uniform Memory Access

Модель UMA (Uniform Memory Access) — это упрощение, к которому привыкли программисты со времён, когда у
процессора был один сокет и одна шина памяти. На UMA любая ячейка RAM достижима за одинаковое время с
любого ядра. На современных серверах эта модель ломается: процессор давно состоит из нескольких
независимых блоков с собственным контроллером памяти, и обращение к «чужой» памяти стоит существенно
дороже, чем к «своей».

**NUMA** (Non-Uniform Memory Access) — архитектура, где RAM физически распределена между несколькими
**nodes** (узлами). Каждый node имеет свой контроллер памяти, свой набор CPU-ядер и свой банк RAM.
Доступ ядра к своему локальному node быстрый; доступ к памяти другого node — медленнее, потому что
запрос идёт через межпроцессорный интерконнект (Intel UPI, AMD Infinity Fabric). Игнорировать NUMA на
многосокетных серверах — терять десятки процентов производительности.

## Откуда NUMA берётся

Исторически — из многосокетных систем. Два процессора, у каждого свой контроллер памяти, между ними —
интерконнект. Доступ к памяти своего сокета — local; к чужой — remote через интерконнект.

```
┌───────────────────────────────────────────────────────────────────┐
│                                                                   │
│   ┌────────── NODE 0 ──────────┐    ┌────────── NODE 1 ──────────┐│
│   │                            │    │                            ││
│   │   ┌────┐ ┌────┐ ┌────┐     │    │   ┌────┐ ┌────┐ ┌────┐     ││
│   │   │CPU0│ │CPU1│ │CPU2│     │    │   │CPU4│ │CPU5│ │CPU6│     ││
│   │   └─┬──┘ └─┬──┘ └─┬──┘     │    │   └─┬──┘ └─┬──┘ └─┬──┘     ││
│   │     │      │      │        │    │     │      │      │        ││
│   │   ┌─┴──────┴──────┴──┐     │    │   ┌─┴──────┴──────┴──┐     ││
│   │   │  L3 cache (shared)│    │    │   │  L3 cache (shared)│    ││
│   │   └─────────┬─────────┘    │    │   └─────────┬─────────┘    ││
│   │             │              │    │             │              ││
│   │   ┌─────────┴─────────┐    │    │   ┌─────────┴─────────┐    ││
│   │   │ memory controller │    │    │   │ memory controller │    ││
│   │   └─────────┬─────────┘    │    │   └─────────┬─────────┘    ││
│   │             │              │    │             │              ││
│   │   ┌─────────┴─────────┐    │    │   ┌─────────┴─────────┐    ││
│   │   │   DRAM 64 GiB     │    │    │   │   DRAM 64 GiB     │    ││
│   │   └───────────────────┘    │    │   └───────────────────┘    ││
│   │                            │    │                            ││
│   └────────────┬───────────────┘    └─────────────┬──────────────┘│
│                │                                  │               │
│                └──────── UPI / Infinity Fabric ───┘               │
│                          ~50 ns доп. латентности                  │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘

CPU0 → DRAM node 0:   local hit,  ~80 ns
CPU0 → DRAM node 1:   remote hit, ~120 ns  (1.5× медленнее)
```

Современные процессоры идут дальше: NUMA-узлы появляются и внутри одного сокета.

| Платформа           | Источник NUMA                                             |
|---------------------|-----------------------------------------------------------|
| Intel Xeon Scalable | сокеты + sub-NUMA clustering (SNC, опционально)           |
| AMD EPYC / TR       | чиплеты (CCD), каждый со своим IOD-связанным контроллером |
| AMD Zen 1/2         | NUMA-per-die: 4 NUMA-узла на одном сокете                 |
| ARM neoverse        | mesh-интерконнект, NUMA-узлы по группам ядер              |
| Apple M1/M2/M3      | UMA внутри чипа, не NUMA (один контроллер)                |

AMD EPYC первого поколения (Naples) в режиме NPS4 показывал четыре NUMA-узла **на каждом сокете** —
итого восемь на двухсокетной системе. Это часто удивляло администраторов, привыкших к «1 сокет = 1 узел».

Big.LITTLE на ARM формально не NUMA (память общая), но похожие проблемы создают разные классы ядер с
разной производительностью; для NUMA-aware планировщика это отдельный класс задач.

## Представление NUMA в Linux

Ядро экспортирует топологию через sysfs:

```
/sys/devices/system/node/
├── node0/
│   ├── cpulist                 ← список CPU этого node (0-7,16-23)
│   ├── meminfo                 ← MemTotal/MemFree/Active/... для node
│   ├── numastat                ← счётчики hit/miss/foreign
│   ├── distance                ← вектор distances до всех node
│   └── hugepages/              ← huge pages по nodes
├── node1/
└── ...
```

Утилита `numactl --hardware` собирает всё в одно место:

```
$ numactl --hardware
available: 2 nodes (0-1)
node 0 cpus: 0 1 2 3 4 5 6 7 16 17 18 19 20 21 22 23
node 0 size: 64278 MB
node 0 free: 32145 MB
node 1 cpus: 8 9 10 11 12 13 14 15 24 25 26 27 28 29 30 31
node 1 size: 64511 MB
node 1 free: 51022 MB
node distances:
node   0   1
  0:  10  21      ← local distance = 10, remote = 21 (×2.1)
  1:  21  10
```

**Distance** — относительный показатель латентности, базовая величина 10 — local. Для двухсокетной
системы remote distance обычно 20-21, для четырёхсокетной — до 30-40 для дальних углов. Это не
наносекунды напрямую, а abstract weights для подсистем ядра.

Файл `/sys/devices/system/node/possible` показывает, сколько NUMA-узлов система может иметь в
принципе; `online` — сколько активно сейчас (hot-plug может менять это число).

## Memory policies

Стратегия аллокации NUMA в Linux задаётся **memory policy** — где брать страницы при page fault.
Системные вызовы `set_mempolicy(2)`, `mbind(2)` и заголовок `<numaif.h>` определяют пять политик:

| Политика              | Поведение                                                   |
|-----------------------|-------------------------------------------------------------|
| `MPOL_DEFAULT`        | local — на node того CPU, где случился page fault           |
| `MPOL_BIND`           | строго на указанном множестве nodes; OOM при нехватке       |
| `MPOL_INTERLEAVE`     | round-robin по nodes; полезно для shared read-mostly данных |
| `MPOL_PREFERRED`      | предпочесть один node, fallback на любой свободный          |
| `MPOL_PREFERRED_MANY` | (5.15+) предпочесть множество nodes, fallback на остальные  |
| `MPOL_LOCAL`          | то же, что DEFAULT — явное имя                              |

Политика может быть установлена:

- **process-wide** через `set_mempolicy(2)` — действует на все будущие аллокации процесса;
- **regional** через `mbind(2)` — для конкретного диапазона виртуальных адресов;
- **VMA-default** через `mbind` с флагом `MPOL_MF_MOVE` — пересаживает уже отображённые страницы.

### Сравнение политик визуально

```
Допустим, 2 NUMA-узла, процесс работает на CPU node 0, выделяет 4 страницы.

  MPOL_DEFAULT (local)             MPOL_BIND (set={0})
  ┌─── node 0 ────┐                ┌─── node 0 ────┐
  │ [p1][p2][p3][p4]               │ [p1][p2][p3][p4]
  └────────────────┘               └────────────────┘
  ┌─── node 1 ────┐                ┌─── node 1 ────┐
  │     (пусто)    │               │     (пусто)    │
  └────────────────┘               └────────────────┘


  MPOL_INTERLEAVE (round-robin)    MPOL_PREFERRED (preferred=1)
  ┌─── node 0 ────┐                ┌─── node 0 ────┐
  │ [p1]    [p3]                   │     (пусто)
  └────────────────┘               └────────────────┘
  ┌─── node 1 ────┐                ┌─── node 1 ────┐
  │     [p2]    [p4]               │ [p1][p2][p3][p4]
  └────────────────┘               └────────────────┘
```

`INTERLEAVE` полезна для read-mostly разделяемых структур: суммарная bandwidth удваивается, потому что
запросы распараллеливаются между двумя контроллерами памяти. На write-heavy данных interleave вредит —
cache line ping-pong через интерконнект убивает производительность.

## numactl: запуск процесса с привязкой

CLI-инструмент для запуска без правки кода:

```bash
# Запустить на CPU node 0, память — только с node 0
numactl --cpunodebind=0 --membind=0 ./app

# Все CPU node 1 разрешены, память — interleave между обоими nodes
numactl --cpunodebind=1 --interleave=0,1 ./app

# Закрепить за конкретными CPU
numactl --physcpubind=0,1,2,3 ./app

# Не привязывая, но переместить уже существующие страницы на node 0
numactl --membind=0 --migrate-existing ./app

# Показать политику текущего процесса
numactl --show
```

Это разумный первый шаг диагностики: запустить нагрузочный тест с `numactl --cpunodebind=0 --membind=0`
и сравнить с дефолтом. Если разница большая — приложение страдает от cross-node трафика и нужны более
тонкие настройки.

## Программный API

### Установка политики для процесса

```c
#include <numaif.h>

unsigned long nodemask = (1UL << 0) | (1UL << 1);   // nodes 0 и 1
set_mempolicy(MPOL_INTERLEAVE, &nodemask, 3);       // 3 = maxnode + 1
```

### Привязка региона памяти

```c
#include <numaif.h>
#include <sys/mman.h>

void *mem = mmap(NULL, 1 << 30, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

unsigned long mask = 1UL << 1;                      // только node 1
mbind(mem, 1 << 30, MPOL_BIND, &mask, 2, MPOL_MF_STRICT);
```

`MPOL_MF_STRICT` возвращает ошибку, если страницы уже расположены не на нужном node. С флагом
`MPOL_MF_MOVE` ядро перенесёт страницы. `MPOL_MF_MOVE_ALL` позволяет двигать даже разделяемые страницы
(требует `CAP_SYS_NICE`).

### Явное перемещение страниц

```c
#include <numaif.h>

void *pages[100];                  // массив адресов страниц
int   nodes[100];                  // куда отправить каждую страницу
int   status[100];                 // фактический node после migration

// заполняем pages[] и nodes[]...
move_pages(0, 100, pages, nodes, status, MPOL_MF_MOVE);
```

`move_pages` сразу мигрирует уже отображённые страницы. Полезно после изменения CPU affinity потока:
сначала переместили поток, потом подтянули его данные.

### Удобная библиотека libnuma

`<numa.h>` (пакет `libnuma-dev`) — высокоуровневая обёртка:

```c
#include <numa.h>

if (numa_available() < 0) {
    // NUMA не поддерживается
}

int max = numa_max_node();
struct bitmask *m = numa_allocate_nodemask();
numa_bitmask_setbit(m, 0);
numa_bitmask_setbit(m, 1);

numa_set_membind(m);                  // INTERLEAVE по nodes 0,1
void *buf = numa_alloc_onnode(SIZE, 0);  // выделить SIZE байт на node 0
numa_free(buf, SIZE);
```

## Auto-balancing: автоматическая миграция

С ядра 3.8 Linux умеет сам мигрировать страницы и потоки на правильные nodes без вмешательства
приложения. Подсистема называется **NUMA balancing** или **autonuma**.

Принцип — periodic page-table scan + faults:

```
                  ┌─────────────────────────────────────┐
                  │  task_numa_work (раз в N мс)        │
                  │  снимает PTE access bit             │
                  │  и делает PTE protection none       │
                  │  для куска адресного пространства   │
                  └────────────────┬────────────────────┘
                                   │
                                   ▼
                  ┌─────────────────────────────────────┐
                  │  при следующем доступе к этим       │
                  │  страницам возникает Page Fault     │
                  │  "NUMA hint fault" (handle_mm_fault)│
                  └────────────────┬────────────────────┘
                                   │
                                   ▼
                  ┌─────────────────────────────────────┐
                  │  ядро запоминает:                   │
                  │  - task → node, где случился fault  │
                  │  - страница → home node             │
                  └────────────────┬────────────────────┘
                                   │
                                   ▼
                  ┌─────────────────────────────────────┐
                  │  ПЕРИОДИЧЕСКИ:                      │
                  │  если task чаще обращается          │
                  │  к страницам не своего node →       │
                  │    мигрировать task или страницу    │
                  │                                     │
                  │  цель — task и его hot pages        │
                  │  на одном node                      │
                  └─────────────────────────────────────┘
```

Управление:

```bash
# Включить/выключить (1 = on, default обычно 1 на серверных дистрибутивах)
sysctl kernel.numa_balancing

# Период сканирования страниц
sysctl kernel.numa_balancing_scan_period_min_ms     # default: 1000
sysctl kernel.numa_balancing_scan_period_max_ms     # default: 60000
```

Auto-balancing — компромисс. Overhead неизбежен (лишние page faults, миграции стоят I/O), но для
большинства long-running серверных нагрузок выигрыш от локализации перевешивает. Для строго
закреплённых процессов (например, под `numactl --cpunodebind`) auto-balancing бесполезен и иногда
вреден — некоторые СУБД отключают его глобально.

## NUMA-aware аллокаторы

Системный `malloc` (glibc ptmalloc2) NUMA не учитывает. Альтернативы умеют лучше:

### jemalloc

С ядра 5.x использует per-CPU arenas (`MALLOC_CONF="percpu_arena:percpu"`): каждое ядро имеет свою
arena, страницы которой аллоцируются local. Контекст-switch между CPU редок, поэтому большинство
аллокаций приходят с локальной node.

### tcmalloc

Имеет per-thread cache + central heap. NUMA-awareness обеспечивается косвенно — через CPU affinity
потоков. Если поток закреплён за одним CPU, его страницы будут в основном local.

### mimalloc

Поддерживает NUMA через separate heaps per process, опционально per-node. На многосокетных серверах
показывает заметный прирост на write-heavy нагрузках.

Общее правило: если приложение чувствительно к NUMA — заменить системный malloc на jemalloc/mimalloc и
включить per-CPU arenas; это работает лучше, чем вручную писать `mbind` под каждый объект.

## Page migration

Linux мигрирует страницы лениво — только в момент, когда становится понятно, что страница нужна на
другом node:

1. Auto-balancing определяет, что task стабильно обращается к чужой странице → миграция страницы.
2. Приложение вызывает `move_pages(2)` или `mbind` с `MPOL_MF_MOVE`.
3. Memory hot-unplug — все страницы с убираемого node принудительно мигрируются.
4. `migrate_pages` через cgroup v2 `cpuset.mems` — при переназначении cgroup на другие nodes.

Стоимость миграции — две page copies (через DMA или обычный memcpy), TLB shootdown на всех CPU и
обновление обратных отображений. На терабайтных working sets миграция может занять минуты, поэтому
auto-balancing работает throttled.

## Диагностика NUMA

### Глобальная статистика

```bash
numastat
#                          node0           node1
# numa_hit             125643211        98453234
# numa_miss               143521          892341      ← попытка local, ушла remote
# numa_foreign            892341          143521      ← страницы remote использованы здесь
# interleave_hit             542             534
# local_node            125499690        97560893
# other_node              143521          892341
```

`numa_miss` — индикатор давления: процессу хотелось local, но local node был занят, выделили remote.
Растёт быстро = плохая локализация.

### По процессу

```bash
# Запустить с детальной разбивкой по node
numastat -p <pid>

# Карта VMA с распределением страниц по nodes
cat /proc/<pid>/numa_maps
# 7f5b8c000000 default file=/lib/x86_64-linux-gnu/libc.so.6 mapped=350 N0=300 N1=50
# 7f5b8c200000 prefer:1 anon=128 dirty=128 N1=128
```

В `numa_maps` для каждого VMA видно: какая политика, сколько страниц на каждом node, в каком состоянии.

### Профилирование remote-доступов

```bash
# Счётчики на уровне CPU: сколько обращений ушли в чужой node
sudo perf stat -e numa-misses,numa-hits,offcore_response.demand_data_rd.* ./app

# Топ функций по NUMA-промахам
sudo perf top -e mem_load_uops_l3_miss_retired.remote_dram
```

Для AMD аналогичные счётчики называются по-другому (`l3_lookup_state.l3_miss_remote_node`); состав
events зависит от микроархитектуры.

### Visual через numaview/likwid

`likwid-topology -g` выдаёт ASCII-карту сокетов, ядер, кэшей и NUMA-доменов. `numatop` (Intel) — TUI с
real-time графиками cross-node трафика по процессам.

## Что обычно делает приложение «NUMA-плохо»

- **Запускается без affinity.** Планировщик кидает task между nodes, страницы остаются на старом node,
  каждый read — remote.
- **Single producer на одном CPU, consumers на другом node.** Producer пишет в локальный кэш, consumer
  читает remote — каждая cache line идёт через интерконнект.
- **Преаллоцирует огромный shared buffer одним потоком.** Все страницы оказываются на node того потока,
  worker'ы на других nodes ходят remote. Решение — first-touch с разных nodes или явный
  `MPOL_INTERLEAVE`.
- **Использует системный malloc.** Без per-CPU arenas страницы из central heap «случайно» на любом node.
- **Игнорирует расположение I/O устройств.** NIC и NVMe-диски тоже подключены к конкретному PCIe-комплексу
  одного node; DMA-буферы должны быть local к устройству.

```
плохой пример: thread на node 1, страницы на node 0
       ┌─────────────┐                 ┌─────────────┐
       │   node 0    │                 │   node 1    │
       │  ┌────────┐ │                 │             │
       │  │ pages  │ │                 │             │
       │  │ (40 GB)│ │                 │             │
       │  └───┬────┘ │                 │             │
       │      │      │                 │  ┌────────┐ │
       │      │      │                 │  │ thread │ │
       │      │      │ ◀──── 50 ns ────┼──┤        │ │
       │      │      │   remote        │  └────────┘ │
       │      │      │                 │             │
       └─────────────┘                 └─────────────┘

каждый доступ = +50 ns латентности, BW падает в разы
```

## Связанные темы

- [Виртуальная память](virtual_memory.md) — страничная организация, на которой строится NUMA-распределение
- [mmap и маппинг файлов](mmap_and_file_mapping.md) — `mmap` + `mbind` для NUMA-aware регионов
- [Реализация malloc и free](malloc_and_free_implementation.md) — почему системный аллокатор не NUMA-aware
- [Приоритеты, аффинность, capabilities](../processes/process_priority_affinity_capabilities.md) — CPU affinity, без неё
  NUMA не имеет смысла
- [Кэши процессора](../assembler_and_processor/cpu_caches.md) — L3 и cache coherence как фундамент NUMA-проблем

## Источники

- `man 7 numa`
- [Kernel Documentation: admin-guide/mm/numa_memory_policy.rst](https://www.kernel.org/doc/html/latest/admin-guide/mm/numa_memory_policy.html)
- [Kernel Documentation: vm/numa.rst](https://www.kernel.org/doc/html/latest/mm/numa.html)
- Ulrich Drepper, [«What Every Programmer Should Know About Memory»](https://akkadia.org/drepper/cpumemory.pdf), 2007 —
  раздел 5 (NUMA)
- `man 2 set_mempolicy`, `man 2 mbind`, `man 2 move_pages`
- `man 3 numa` (libnuma)
- [LWN: AutoNUMA](https://lwn.net/Articles/486858/)
- [Brendan Gregg: NUMA observability](https://www.brendangregg.com/blog/2017-01-17/numa.html)
