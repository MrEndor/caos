# cgroups v2: управление ресурсами процессов

**cgroups** (control groups) и **namespaces** — два независимых kernel-механизма, на которых построены все
Linux-контейнеры. Их часто путают, но задачи у них противоположные:

```
┌────────────────────────────┬─────────────────────────────────────┐
│        namespaces          │              cgroups                │
├────────────────────────────┼─────────────────────────────────────┤
│ изолируют то, что процесс  │ ограничивают то, что процесс        │
│ ВИДИТ                      │ ИСПОЛЬЗУЕТ                          │
├────────────────────────────┼─────────────────────────────────────┤
│ PID, mount, network, IPC,  │ CPU time, RAM, I/O bandwidth,       │
│ UTS, user, cgroup, time    │ число процессов, network packets    │
└────────────────────────────┴─────────────────────────────────────┘
                  │                          │
                  └────────────┬─────────────┘
                               ▼
                          контейнер
```

cgroups появились в 2007 (Paul Menage, Rohit Seth, Google) для нужд внутренней инфраструктуры. Первый дизайн (**v1**)
разрастался по одному controller'у в год — каждый со своей иерархией, своими файлами, своими правилами. К 2014 году
такой разнобой стал нерабочим. В 2016 Tejun Heo переписал всё в **cgroups v2**: одна unified hierarchy, единые правила,
согласованная семантика. С 2019 (RHEL 9, Fedora 31+, Debian 11+, Ubuntu 22.04+) v2 — дефолт.

## v1 vs v2

| Аспект                | v1                                 | v2                               |
|-----------------------|------------------------------------|----------------------------------|
| Иерархия              | по одной на controller             | одна для всех controllers        |
| Процесс в группах     | мог быть в разных по cpu/memory/io | ровно в одной cgroup             |
| Активация controllers | через mount option                 | `cgroup.subtree_control`         |
| Делегирование         | трудно, ad-hoc                     | по дизайну, через `Delegate=yes` |
| memory.use_hierarchy  | флаг, путаница с наследованием     | hierarchy всегда                 |
| PSI                   | нет                                | есть, per-cgroup                 |
| freezer               | отдельный controller               | встроен (`cgroup.freeze`)        |
| devices controller    | был                                | удалён, замена — eBPF            |

В v1 процесс мог быть, например, в `/cpu/web` и `/memory/database` одновременно — controllers жили в разных деревьях.
Это позволяло гибкость, но ломало любую внятную семантику наследования и приоритезации. В v2 одна группа = один набор
лимитов на все ресурсы; это упрощает рассуждения и реализацию.

Mixed mode (часть controllers в v1, часть в v2) технически возможен, но на современных системах смысла в нём нет — все
controllers, нужные в практике, есть в v2.

## Архитектура v2

Корень v2-иерархии монтируется ядром в `/sys/fs/cgroup` (тип `cgroup2`). Поддиректории — это cgroups; `mkdir` создаёт
группу, `rmdir` удаляет (только пустую). Внутри каждой директории — набор файлов с фиксированными именами:

```
/sys/fs/cgroup/mygroup/
├── cgroup.procs              ← список PID процессов в этой группе
├── cgroup.threads            ← список TID (если включён thread mode)
├── cgroup.controllers        ← controllers, доступные здесь (read-only)
├── cgroup.subtree_control    ← controllers, активные в детях (rw)
├── cgroup.events             ← populated 0/1, frozen 0/1
├── cgroup.freeze             ← 0/1, заморозить процессы
├── cgroup.max.depth          ← макс. глубина поддерева
├── cgroup.max.descendants    ← макс. число потомков
├── cpu.weight, cpu.max, cpu.stat, ...
├── memory.max, memory.high, memory.current, memory.stat, ...
├── io.max, io.weight, io.stat
├── pids.max, pids.current
└── cpu.pressure, memory.pressure, io.pressure   ← PSI
```

### No internal processes rule

Главное ограничение v2: **если группа включила хотя бы один controller в `cgroup.subtree_control`, она не может
содержать процессы напрямую** — только в листьях. Это исключает двусмысленность («чьим лимитам подчиняется процесс в
промежуточном узле — своим или дочерним?»). Root cgroup — единственное исключение.

```
ПРАВИЛЬНО                            НЕПРАВИЛЬНО

       root                                root
        │ subtree_control: cpu              │ subtree_control: cpu
   ┌────┴────┐                          ┌───┴────┐
   │ web/    │ ← листья,                │ app/   │  ← есть subtree_control,
   │ db/     │   содержат PIDs          │ PID 5  │ ✗   не может
   └─────────┘                          └────────┘     содержать PID
                                        EBUSY при echo 5 > cgroup.procs
```

### Иерархия systemd

systemd на старте монтирует `/sys/fs/cgroup` и создаёт стандартную иерархию slices/scopes/services:

```
/sys/fs/cgroup/                                ← root cgroup
│
├── init.scope/                                ← сам PID 1 (systemd)
│   └── cgroup.procs                          [1]
│
├── system.slice/                              ← все system-сервисы
│   ├── nginx.service/
│   │   └── cgroup.procs                      [nginx pids]
│   ├── postgresql.service/
│   │   └── cgroup.procs                      [postgres pids]
│   ├── docker.service/
│   │   ├── cgroup.procs                      [dockerd]
│   │   └── docker-abc123.scope/               ← контейнер
│   │       └── cgroup.procs                  [containerd-shim, app]
│   └── ...
│
├── user.slice/                                ← пользовательские сессии
│   └── user-1000.slice/
│       ├── user@1000.service/                 ← user systemd
│       │   ├── app.slice/
│       │   │   └── firefox.service/
│       │   └── session.slice/
│       └── session-3.scope/                   ← TTY/SSH сессия
│           └── cgroup.procs                  [bash, ssh]
│
└── machine.slice/                             ← VMs / nspawn-контейнеры
    └── machine-mycontainer.scope/
```

- **slice** — узел-контейнер, группирует другие slices/services/scopes; не содержит процессов сам по себе.
- **service** — управляется unit-файлом (`nginx.service`), systemd запускает процессы внутрь.
- **scope** — группа уже существующих процессов, не запущенных systemd (TTY-сессии, контейнеры внешних рантаймов).

## Базовые операции

```bash
# Создать cgroup
mkdir /sys/fs/cgroup/mygroup

# Активировать controllers в поддереве (на родителе)
echo "+memory +io +pids" > /sys/fs/cgroup/cgroup.subtree_control

# Что доступно / что активно
cat /sys/fs/cgroup/mygroup/cgroup.controllers       # доступные controllers
cat /sys/fs/cgroup/mygroup/cgroup.subtree_control   # активные у потомков

# Переместить процесс
echo $$ > /sys/fs/cgroup/mygroup/cgroup.procs       # текущий shell сюда

# Посмотреть состояние
cat /sys/fs/cgroup/mygroup/cgroup.procs             # кто внутри
cat /sys/fs/cgroup/mygroup/memory.current           # сколько занято

# Удалить (только пустую — без процессов и подгрупп)
rmdir /sys/fs/cgroup/mygroup
```

Перемещение процесса между cgroups — это `write(2)` PID в `cgroup.procs` целевой группы. Источником может быть любая
группа; разрешение зависит от capabilities (`CAP_SYS_ADMIN` для root, либо делегирование).

## CPU controller

Файлы CPU controller в v2: `cpu.weight` (soft relative priority, 1..10000), `cpu.max` (hard cap, формат
`MAX_USAGE PERIOD`), `cpu.stat` (usage/throttle counters), `cpuset.cpus` / `cpuset.mems` (NUMA affinity).

Подробности — см. [Приоритеты, affinity, capabilities](../processes/process_priority_affinity_capabilities.md), раздел
«cgroups v2 — CPU controller». Дальше — controllers, которых там нет.

## Memory controller

Memory — самый сложный controller, потому что «память» в Linux — это не одно число. Page cache, anon pages, kernel
stacks, slab, socket buffers, shmem — у каждой категории своя политика учёта и reclaim'а. v2 учитывает всё это в одном
`memory.current` (в отличие от v1, где kernel memory был отдельным controller'ом).

### Иерархия лимитов

```
                      реальное потребление group'ы
                                  │
                                  ▼
   memory.min ───────► memory.low ─────► memory.high ──────► memory.max
   защита от reclaim   защита от           soft limit:        hard limit:
   (HARD: не отдаст    reclaim (best-     throttling           OOM-killer
    память даже под    effort, под        записывающих         внутри cgroup
    OOM)               OOM отдаст)        задач

   ◀── защита ──────────────────────────────────────── ограничение ──▶
```

### Файлы

| Файл                  | Что делает                                                         |
|-----------------------|--------------------------------------------------------------------|
| `memory.max`          | hard limit. Превышение → reclaim, не помог → cgroup-local OOM      |
| `memory.high`         | soft limit. Превышение → throttle всех аллокаций в группе          |
| `memory.low`          | best-effort защита: kernel избегает reclaim'ить страницы из группы |
| `memory.min`          | hard защита: вообще не reclaim'ит (рискует глобальным OOM)         |
| `memory.current`      | сумма всех видов памяти, относящейся к группе                      |
| `memory.peak`         | максимум `memory.current` с момента создания группы                |
| `memory.stat`         | детализация: anon, file, sock, kernel_stack, slab, shmem, thp, ... |
| `memory.events`       | счётчики: low, high, max, oom, oom_kill                            |
| `memory.swap.max`     | hard limit на swap для группы                                      |
| `memory.swap.current` | сколько swap'а сейчас используется                                 |
| `memory.oom.group`    | 1 → при OOM убивать всю группу одним махом, не отдельный процесс   |
| `memory.reclaim`      | принудительно reclaim'ить N байт (write-only)                      |

### memory.max и OOM

При `memory.current > memory.max` kernel сначала пытается `try_to_free_mem_cgroup_pages()`: сбрасывает чистый page
cache, запускает writeback на dirty, по возможности — swap. Если освободить не удаётся — срабатывает **cgroup-local
OOM-killer**: выбирается жертва **только из процессов этой cgroup**, не глобально. Это критическое отличие: OOM в одном
контейнере не убивает соседа.

```
memory.current                                  memory.max
     │                                              │
     ▼                                              ▼
┌─────────────────────────────────────┬──────────────┐
│ anon │ file │ sock │ slab │ shmem   │  reclaim     │
└─────────────────────────────────────┴──────────────┘
                                              │
                                              ▼
                                      выкинули чистый
                                      page cache
                                              │
                                              ▼
                                      memory.events.max++
                                              │
                                              ▼
                                      не помогло?
                                              │
                                              ▼
                                      OOM-killer выбирает
                                      жертву из cgroup.procs
                                              │
                                              ▼
                                      memory.events.oom_kill++
```

`memory.oom.group=1` превращает cgroup-local OOM в «убить всю группу». Полезно для services, где смерть одного процесса
оставляет систему в неконсистентном состоянии — лучше убить целиком и дать systemd рестартовать.

### memory.high и throttling

`memory.high` — мягкий лимит. При превышении ядро **не убивает процесс**, а **замедляет его аллокации**: каждый page
fault в группе начинает занимать дополнительное время (расчётно пропорциональное превышению). Это даёт процессу шанс
самому понять, что памяти мало, и освободить что-нибудь — без катастрофы.

Использование: ставить `memory.high` чуть ниже `memory.max`. При нормальной нагрузке группа не упирается; при аномалии
throttling сигнализирует через `memory.events.high` (счётчик растёт), и оператор успевает среагировать до OOM.

### memory.low и memory.min

Эти два файла защищают группу **от реклейма со стороны других groups**. Сценарий: на хосте крутится database с
`memory.low=10G` и batch-job без защиты. Когда batch-job просит больше памяти и kernel запускает reclaim, он сначала
ищет страницы у тех, кто **выше** `memory.low`; у database — только в крайнем случае.

- `memory.low` — best-effort. Под глобальным memory pressure ядро **может** забрать страницы из защищённой группы.
- `memory.min` — hard. Ядро **никогда** не отдаст страницы под защитой `min`, даже ценой глобального OOM.

### Учёт page cache

`memory.current` включает page cache, привязанный к этой группе. Это значит, что **большой write в файл может довести
группу до `memory.max`** — даже если у процесса всего 100 MB anon-памяти, page cache от записываемого файла попадёт в
его cgroup. Без понимания этого легко получить «загадочный» OOM на копировании файла.

```bash
cat /sys/fs/cgroup/mygroup/memory.stat
# anon          524288000        ← полезная heap/stack
# file          2147483648       ← page cache  (вот он растёт)
# kernel_stack  4194304
# slab          12582912
# sock          1048576
# shmem         0
# anon_thp      0
# ...
```

### Связь с PSI

Memory controller тесно связан с PSI: `memory.pressure` показывает, сколько времени задачи в этой группе простояли в
ожидании памяти. Это лучший сигнал «группа упирается в лимит», чем `memory.current` сам по себе — потому что фиксирует *
*импакт на работу**, а не только факт использования.

## IO controller

IO controller v2 управляет block-устройствами, идентифицируемыми парой `MAJOR:MINOR` (см. `lsblk`, `/proc/diskstats`).

### Файлы

| Файл          | Что задаёт                                                    |
|---------------|---------------------------------------------------------------|
| `io.weight`   | относительный вес (1..10000, default 100) при конкуренции     |
| `io.max`      | hard limit: `rbps=N wbps=N riops=N wiops=N` per-device        |
| `io.stat`     | rbytes, wbytes, rios, wios, dbytes (discard), dios per-device |
| `io.pressure` | PSI для I/O                                                   |

### Формат io.max

```bash
# 10 MB/s чтение, 5 MB/s запись, 1000 IOPS для /dev/sda (8:0)
echo "8:0 rbps=10485760 wbps=5242880 riops=1000 wiops=1000" \
     > /sys/fs/cgroup/mygroup/io.max

# Снять отдельный лимит
echo "8:0 rbps=max" > /sys/fs/cgroup/mygroup/io.max
```

### io.weight и buffered writes

`io.weight` работает хорошо для **direct I/O** и для **synchronous writes**: kernel знает, какая cgroup вызвала I/O, и
применяет вес.

С **buffered writes** сложнее. Когда `write(2)` кладёт данные в page cache, реальный I/O происходит позже — из потока *
*kworker** writeback, не из контекста процесса. Чтобы правильно отнести этот writeback к правильной cgroup, ядру нужно
отслеживать «кто грязнил эту страницу» — это делает **memory cgroup ownership** (memcg). Если memory controller не
активирован в той же группе — buffered I/O может уйти в неправильную cgroup. Поэтому **io controller почти всегда
включают вместе с memory**.

### I/O scheduler

io controller работает поверх block-layer scheduler'а. Для разных scheduler'ов поддержка cgroup разная:

- **bfq** — лучшая интеграция с io.cgroup, поддерживает и weights и max
- **mq-deadline** — поддерживает io.max через throttling, weights ограниченно
- **none** — никакой scheduler-логики, throttling работает на уровне blk-cgroup, weights — нет

```bash
cat /sys/block/sda/queue/scheduler         # [bfq] mq-deadline none
echo bfq > /sys/block/sda/queue/scheduler
```

### Верификация

```bash
cat /sys/fs/cgroup/mygroup/io.stat
# 8:0 rbytes=524288000 wbytes=104857600 rios=12500 wios=2500 ...

iostat -x 1                                # внешняя картина
cat /proc/diskstats                        # «сырые» счётчики ядра
```

## PIDs controller

`pids.max` — потолок на число tasks (процессов **и** потоков) в группе. Защищает от fork-bomb и от случайного
thread-leak'а, который иначе исчерпает глобальный `kernel.pid_max`.

```bash
echo 100 > /sys/fs/cgroup/mygroup/pids.max
cat /sys/fs/cgroup/mygroup/pids.current        # 23
cat /sys/fs/cgroup/mygroup/pids.events         # max 0
```

При исчерпании — `fork(2)` и `clone(2)` возвращают `EAGAIN`. Это касается и `pthread_create` (внутри — `clone`), и не
только новых процессов. Lazy thread-pool, который растёт по нагрузке, может неожиданно упереться.

`pids.max max` — без лимита. Обычно ставится для каждого пользовательского namespace и контейнера: `pids.max=4096` или
около того.

## HugeTLB, RDMA, misc

- **hugetlb** — лимиты на HugePages (2 MB, 1 GB). Файлы вида `hugetlb.2MB.max`, `hugetlb.1GB.current`. Нужно для
  приложений с явным `MAP_HUGETLB`.
- **rdma** — лимиты на RDMA-объекты (hca_handle, hca_object) per-device. Используется в HPC и сетях с InfiniBand/RoCE.
- **misc** — generic-контроллер для счётных ресурсов, не покрытых остальными (например, SEV ASIDs для confidential VMs).
- **perf_event** — позволяет привязать `perf_event_open` к cgroup для замера счётчиков только её процессов.

Для большинства приложений эти controllers неинтересны; включаются по необходимости.

## PSI (Pressure Stall Information)

PSI появился в Linux 4.20 (декабрь 2018, Facebook, Johannes Weiner). Это попытка ответить на старый вопрос: **что значит
«система перегружена»?** Loadavg показывает run-queue + uninterruptible, но не различает «много CPU-bound задач» и «всё
стоит из-за памяти». PSI разделяет давление на три ресурса: CPU, memory, IO — и считает его per-cgroup.

### Формат

```bash
cat /sys/fs/cgroup/mygroup/memory.pressure
# some avg10=12.34 avg60=8.21  avg300=4.05 total=15234567
# full avg10=2.11  avg60=1.45  avg300=0.72 total=3421000
```

- **some** — хотя бы один task в cgroup был stalled (ждал ресурса)
- **full** — **все** non-idle tasks в cgroup были stalled (для memory/io; для cpu файла full нет — он не имеет смысла на
  root уровне)
- **avg10/60/300** — процент времени за последние 10/60/300 секунд
- **total** — суммарное время в микросекундах с создания cgroup

### Интерпретация

```
some=0   full=0   ─── ресурс не давит, всё OK
some>0   full=0   ─── некоторые tasks ждут, но другие работают
some>>0  full>>0  ─── вся группа фактически стоит — ресурс является bottleneck'ом
```

```
Время  ──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶──▶
        ┌────────────────────────────────────────────────────────────┐
Task 1: │ run │ wait │     run     │  wait  │   run  │ wait │ run    │
        ├────────────────────────────────────────────────────────────┤
Task 2: │  run  │      wait        │   run   │   wait   │   run      │
        ├────────────────────────────────────────────────────────────┤
Task 3: │   run    │   wait   │      run     │   wait   │    run     │
        └────────────────────────────────────────────────────────────┘
              │      │            │       │       │
              │      └─ some=1    │       │       └─ some=0
              │                   │       └─ full=1 (ВСЕ ждут)
              └─ some=0           └─ some=1
```

### Применение

- **systemd-oomd** — userspace OOM-killer, читает `memory.pressure` всех slices; убивает victim **до** того, как
  сработал kernel-OOM. Лучше для интерактивных систем: kernel-OOM срабатывает в самый тяжёлый момент, когда уже всё
  легло; PSI ловит проблему раньше.
- **Autoscaling** — Kubernetes VPA / cluster-autoscaler могут использовать PSI вместо или вместе с `memory.current`.
  Высокий `memory.pressure` без переполнения `memory.max` — сигнал, что нужно подкинуть памяти даже если лимит формально
  не достигнут.
- **Trigger через poll** — с Linux 5.2 можно `poll(2)` файл pressure и получить wake-up при превышении порога.
  Регистрация: `write(fd, "some 150000 1000000", ...)` — будить, когда `some` превысил 150 ms за окно 1 s.

## systemd и cgroups

systemd — главный пользователь cgroups в современной системе. На старте он становится «cgroup manager»: монтирует
`/sys/fs/cgroup`, создаёт slices, размещает все запускаемые services в соответствующие cgroups, проксирует через
unit-файлы изменения лимитов в файлы controllers.

### Slices, services, scopes

| Тип unit | Содержит         | Создаётся         | Пример                                |
|----------|------------------|-------------------|---------------------------------------|
| slice    | другие units     | unit-файл         | `system.slice`, `user-1000.slice`     |
| service  | процессы systemd | `systemctl start` | `nginx.service`                       |
| scope    | внешние процессы | API systemd       | `session-3.scope`, `docker-abc.scope` |

### Resource control из unit-файла

```ini
# /etc/systemd/system/nginx.service.d/limits.conf
[Service]
MemoryMax = 2G
MemoryHigh = 1.5G
CPUWeight = 200
CPUQuota = 50%                ; → cpu.max
IOWeight = 200
TasksMax = 512                ; → pids.max
IODeviceLatencyTargetSec = /dev/sda 5ms
```

Эти ключи systemd транслирует в соответствующие файлы cgroup:

| systemd-property                 | cgroup-файл         |
|----------------------------------|---------------------|
| `MemoryMax`                      | `memory.max`        |
| `MemoryHigh`                     | `memory.high`       |
| `MemoryMin`                      | `memory.min`        |
| `MemorySwapMax`                  | `memory.swap.max`   |
| `CPUWeight`                      | `cpu.weight`        |
| `CPUQuota` / `CPUQuotaPeriodSec` | `cpu.max`           |
| `AllowedCPUs`                    | `cpuset.cpus`       |
| `IOWeight`                       | `io.weight`         |
| `IOReadBandwidthMax`             | `io.max rbps=...`   |
| `TasksMax`                       | `pids.max`          |
| `ManagedOOMSwap`                 | systemd-oomd config |

```bash
# Изменить лимит на лету (применится без рестарта)
systemctl set-property nginx.service MemoryMax=2G

# Применить и записать в drop-in
systemctl set-property --runtime=false nginx.service MemoryMax=2G

# Запустить процесс в transient scope с лимитом
systemd-run --scope -p MemoryMax=1G -p CPUWeight=50 ./myprog
```

### Инструменты systemd

```bash
systemd-cgls                           # дерево cgroups как ps-style tree
systemd-cgtop                          # top по cgroups: CPU%, Memory, Input/s, Output/s
systemctl status nginx.service         # покажет cgroup, лимиты, текущее использование
```

### systemd-oomd

Демон, который читает PSI всех slices и убивает их до того, как сработает kernel-OOM. Конфигурируется per-slice:

```ini
[Slice]
ManagedOOMMemoryPressure = kill
ManagedOOMMemoryPressureLimit = 50%
```

«Если `memory.pressure some avg10` в этом slice превысит 50% — убить самую жирную cgroup внутри.» Это превентивный
механизм: вместо «упало насмерть, перезагрузить» — «прибили один процесс, остальные живут».

## Делегирование cgroups

Делегирование — передача управления поддеревом непривилегированному пользователю или сервису. Без него все операции с
cgroups требуют root. С делегированием — владелец поддерева может создавать sub-cgroups, перемещать процессы между ними,
менять лимиты **в пределах поддерева** (не повышая выше родительских).

```ini
# systemd unit с делегированием
[Service]
Delegate = yes                              # передать всё поддерево
Delegate = cpu memory pids                  # выборочно
```

systemd `chown`'ит файлы `cgroup.procs`, `cgroup.subtree_control` и controller-specific под uid сервиса, что разрешает
запись без CAP_SYS_ADMIN.

**Rootless контейнеры (Podman, rootless Docker)** работают именно через делегирование: пользователь `endor` (uid 1000)
запускает контейнер, который попадает в `/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/user.slice/...`.
Внутри этого поддерева user@1000 — владелец, может создавать собственные cgroups для контейнеров без sudo.

```
/sys/fs/cgroup/
└── user.slice/                                 ← root-owned, delegated дальше
    └── user-1000.slice/                        ← uid 1000 owned
        └── user@1000.service/                  ← user systemd
            └── app.slice/
                └── podman-12345.scope/         ← контейнер
                    ├── cgroup.procs            ← подконтролен uid 1000
                    └── memory.max
```

## freezer через v2

`cgroup.freeze` — однобитный файл: `1` — заморозить все процессы группы (включая потомков), `0` — разморозить.

```bash
echo 1 > /sys/fs/cgroup/mygroup/cgroup.freeze
cat /sys/fs/cgroup/mygroup/cgroup.events
# populated 1
# frozen 1                                  ← переход состоялся

echo 0 > /sys/fs/cgroup/mygroup/cgroup.freeze
```

Снаружи похоже на массовый SIGSTOP, но с принципиальными отличиями:

- **Атомарность**: процессы замораживаются как набор; невозможна гонка, при которой часть уже остановлена, а часть
  успела ответвить ребёнка.
- **Иммунитет к сигналам**: замороженный процесс не реагирует даже на SIGKILL до разморозки (точнее: SIGKILL отложится).
- **Наследование**: новые форки автоматически замораживаются.

Применение: **CRIU** (checkpoint/restore) — сначала заморозить всю группу, потом сделать консистентный снапшот
памяти/состояния; снапшоты файловых систем, миграция контейнеров между хостами.

## eBPF cgroup programs

eBPF позволяет прикреплять программы к cgroup для фильтрации/наблюдения событий, происходящих с процессами этой группы.

| Тип BPF-программы                | На что хук                | Что заменяет / зачем         |
|----------------------------------|---------------------------|------------------------------|
| `BPF_PROG_TYPE_CGROUP_SKB`       | приём/отправка пакетов    | network policy per-cgroup    |
| `BPF_PROG_TYPE_CGROUP_SOCK`      | `socket/bind/connect`     | контроль соединений          |
| `BPF_PROG_TYPE_CGROUP_SOCK_ADDR` | `connect/sendmsg/recvmsg` | переписывание адресов        |
| `BPF_PROG_TYPE_CGROUP_DEVICE`    | `open` device file        | замена devices controller v1 |
| `BPF_PROG_TYPE_CGROUP_SYSCTL`    | чтение/запись sysctl      | namespaced sysctl            |

`devices` controller v1 был удалён в v2 — вместо него Linux требует написать BPF-программу типа `CGROUP_DEVICE`, которая
фильтрует `open(2)` на character/block-устройства. Container runtimes (runc, crun) уже делают это автоматически, когда
видят, что хост на v2.

eBPF — отдельная большая тема; здесь упомянут лишь tie-in с cgroups.

## Tooling

| Инструмент                         | Откуда    | Чем удобен                         |
|------------------------------------|-----------|------------------------------------|
| прямой `/sys/fs/cgroup`            | kernel    | нет зависимостей, всё видно        |
| `systemd-cgls`                     | systemd   | дерево cgroups c PIDs/cmdline      |
| `systemd-cgtop`                    | systemd   | top по cgroups, online             |
| `systemd-run`                      | systemd   | запуск процесса в transient cgroup |
| `systemctl set-property`           | systemd   | изменение лимитов на лету          |
| `cgcreate`, `cgexec`, `cgclassify` | libcgroup | старый CLI; на v2 мало смысла      |
| `lscgroup`                         | libcgroup | плоский листинг всех cgroups       |
| `bpftool cgroup`                   | bpftool   | список BPF-программ на cgroups     |

В практике на современных системах работают либо напрямую с `/sys/fs/cgroup`, либо через systemd. libcgroup-утилиты
остались с эпохи v1 и редко нужны.

## Подводные камни

- **Page cache в memory.max**. При интенсивной записи файла page cache раздувается и попадает в учёт cgroup; группа
  может OOM'нуть на ровном месте. Контрмеры: `O_DIRECT`, периодический `posix_fadvise(POSIX_FADV_DONTNEED)`, либо
  `memory.high` ниже `memory.max` для раннего сигнала.
- **CPU throttling latency**. `cpu.max 50000 100000` означает «не более 50 ms CPU-времени за 100 ms окно». Если задача
  израсходовала бюджет за первые 30 ms — она ждёт **до конца периода** (70 ms простоя), даже если других претендентов
  нет. Это бьёт по p99-latency. Уменьшение `PERIOD` (`cpu.max 10000 20000` вместо `50000 100000`) даёт более равномерное
  распределение, но больше overhead.
- **`pids.max` блокирует pthread_create**. `pthread_create` под капотом — `clone(2)` с `CLONE_THREAD`, потоки считаются
  в pids. Java-приложение с большим thread-pool может неожиданно поймать `EAGAIN`.
- **Удаление непустой cgroup**. `rmdir` вернёт `EBUSY`, пока внутри есть процессы или подгруппы. Сначала переместить
  процессы (`echo PID > .../parent/cgroup.procs`) или дождаться их завершения.
- **buffered writes без memory controller**. Если io включён, а memory нет — buffered I/O может приписаться не к той
  cgroup. Включать парой.
- **subtree_control нельзя менять, когда есть процессы в группе**. EBUSY. Сначала создать дочернюю cgroup, перенести
  процессы туда, потом править subtree_control родителя.
- **Mixed v1/v2**. Технически возможен (часть controllers смонтирована в v1, часть в v2), но семантика страдает. На
  современных дистрибутивах — чистый v2.
- **swap accounting**. На v2 swap всегда учитывается. На v1 нужен был `swapaccount=1` boot-параметр; пережиток,
  обнаружить который сейчас можно только в старых tutorials.
- **OOM в host vs OOM в cgroup**. cgroup-local OOM не пишет в `dmesg` хоста по тем же правилам, что глобальный. Чтобы не
  пропустить — мониторить `memory.events.oom_kill` per-cgroup.

## Связанные темы

- [Linux namespaces](namespaces.md) — изоляция (что процесс видит) vs cgroups (что использует); вместе образуют
  контейнер
- [Приоритеты, affinity, capabilities](../processes/process_priority_affinity_capabilities.md) — CPU controller в
  подробностях
- [rlimit](../processes/rlimit.md) — старый per-process механизм лимитов; до сих пор работает параллельно cgroups
- [seccomp](seccomp.md) — sandbox через фильтрацию syscalls, дополняет cgroups
- [Основы процессов](../processes/processes_basics.md) — PID, fork, что вообще учитывается

## Источники

- [cgroup-v2 — kernel.org](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html)
- [PSI — kernel.org](https://www.kernel.org/doc/html/latest/accounting/psi.html)
- `man 7 cgroups`, `man 5 systemd.resource-control`, `man 1 systemd-cgtop`, `man 1 systemd-run`
- [LWN: control groups, v2 — Jonathan Corbet](https://lwn.net/Articles/679786/)
- [LWN: PSI](https://lwn.net/Articles/759781/)
- [Facebook engineering: resctl-demo (cgroup v2 in production)](https://engineering.fb.com/2018/07/19/production-engineering/resctl-demo/)
- [systemd-oomd documentation](https://www.freedesktop.org/software/systemd/man/systemd-oomd.service.html)
