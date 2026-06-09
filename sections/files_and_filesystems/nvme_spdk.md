# NVMe и SPDK: kernel bypass для storage

Когда SSD стал быстрее, чем способен прокачать через себя классический SATA-стек, появилась необходимость в новом
протоколе, спроектированном под flash и параллелизм PCIe. Так родился **NVMe** (Non-Volatile Memory Express). А когда
и сам ядерный block layer стал упираться в свои накладные расходы — родился **SPDK** (Storage Performance Development
Kit), kernel bypass-driver, выносящий весь storage I/O в userspace.

Эта статья разбирает, чем NVMe устроен иначе, чем SATA/SAS, как реализована его очередная архитектура, что такое
NVMe over Fabrics и ZNS, и зачем (и кому) нужен SPDK против обычного ядерного драйвера.

## Почему NVMe, а не SATA/SAS

SATA и SAS — протоколы из эпохи, когда «диск» означал блин, шпиндель и движущуюся головку. У такой геометрии главным
ограничением была латентность позиционирования (5–10 мс), и одна очередь команд (NCQ глубиной 32) с лихвой покрывала
способность HDD что-либо обработать. Любая параллельность ниже физического шпинделя была бесполезна.

Flash перевернул эту модель. NAND-кристаллы внутри SSD физически параллельны: контроллер обращается одновременно к
десяткам каналов, на каждом — несколько die. Узкое место сместилось с механики на интерфейс, а интерфейс остался от
HDD. SATA3 даёт ~600 MB/s и одну очередь — выше этого SSD не может, как бы быстро ни был флеш.

|                   | SATA 3.0  | SAS 12G  | NVMe (PCIe Gen4 x4)         |
|-------------------|-----------|----------|-----------------------------|
| Bandwidth (max)   | 600 MB/s  | 1.2 GB/s | ~7 GB/s                     |
| Очередей          | 1         | 1        | до 65535 (одна admin + I/O) |
| Глубина очереди   | 32 (NCQ)  | 254      | до 65536 на очередь         |
| Латентность стека | ~100 µs   | ~80 µs   | ~10 µs                      |
| Прерывания        | один IRQ  | один IRQ | MSI-X, по одному на очередь |

NVMe — это спецификация (`NVM Express`), описывающая прямой transport между host и SSD поверх PCIe. Никакого SATA-моста,
никакого AHCI: команды идут в кольцевые буферы в shared memory, контроллер дёргает MSI-X interrupt напрямую на конкретное
ядро, обработавшее запрос. Протокол изначально спроектирован под flash и под многоядерные CPU.

## NVMe: основы

NVMe построен вокруг нескольких базовых сущностей.

**Controller** — собственно устройство (NVMe SSD как PCIe endpoint). Один контроллер обслуживает один host и один набор
очередей. У SSD-карты enterprise-класса часто несколько контроллеров на одной железке (для multi-host доступа).

**Namespace** — диапазон логических адресов (LBA), который видится host'у как отдельный «диск». Это аналог партиции, но
на уровне самого устройства: один SSD может экспортировать несколько namespace'ов с разными размерами блока, разной
форматизацией (с/без metadata), разными настройками защиты данных. В Linux они появляются как `/dev/nvme0n1`,
`/dev/nvme0n2` — `n` означает namespace.

**Submission Queue (SQ) и Completion Queue (CQ)** — пара кольцевых буферов в памяти host'а:

- SQ — host пишет туда команды (read, write, flush, …).
- CQ — controller пишет туда результаты выполнения.

Каждая SQ привязана к одной CQ (одна CQ может обслуживать несколько SQ). Это разделение нужно потому, что отправка и
получение происходят независимо: host может закидывать новые команды, пока контроллер ещё обрабатывает старые.

```
 host RAM                                  NVMe controller (PCIe device)

 ┌────────────────────────────────────┐    ┌────────────────────────────┐
 │  Submission Queue (SQ, ring)       │    │  controller                │
 │                                    │    │                            │
 │   head ──▶ [ C0 ][ C1 ][ C2 ][..]  │    │  1. читает SQ через DMA    │
 │                              ▲     │    │  2. исполняет операцию     │
 │                              tail  │    │     (read/write flash)     │
 └────────────────────────────────────┘    │  3. пишет completion entry │
                  │                        │     в CQ                   │
                  │  SQ tail doorbell      │  4. шлёт MSI-X interrupt   │
                  └──────────(MMIO)───────▶│                            │
                                           │                            │
 ┌────────────────────────────────────┐    │                            │
 │  Completion Queue (CQ, ring)       │ ◀──┤  DMA пишет CE              │
 │                                    │    │                            │
 │   head ──▶ [ E0 ][ E1 ][ E2 ][..]  │    └────────────────────────────┘
 │                              ▲     │
 │                              tail  │
 └─────────────────────┬──────────────┘
                       │
        ▲ MSI-X interrupt прилетает на CPU, привязанный к этой CQ
        │
        └── host обрабатывает CE, обновляет CQ head doorbell (MMIO)
```

Цикл выглядит так:

1. Host формирует **command** в SQ — 64-байтовая структура (opcode, namespace, LBA, длина, PRP/SGL).
2. Host увеличивает **SQ tail doorbell** — 4-байтовый MMIO write в регистр контроллера. Это говорит controller'у: «у меня
   новая работа».
3. Controller через DMA читает команду из SQ, исполняет (читает или пишет flash), формирует **completion entry** (16
   байт) в CQ.
4. Controller шлёт **MSI-X interrupt** на ядро, привязанное к этой CQ.
5. Host обрабатывает completion, инкрементит **CQ head doorbell** — говорит «слот свободен».

Doorbell — единственное MMIO, которое нужно на команду. Всё остальное идёт через DMA — controller сам читает SQ и
данные. Это и даёт сверхнизкую латентность: один write в регистр на отправку, один interrupt на получение.

## Admin queue и I/O queues

При старте у контроллера есть только одна пара — **admin queue**. Через неё host:

- запрашивает идентификацию контроллера и namespace'ов (`Identify` command),
- форматирует namespace,
- создаёт/удаляет I/O queues,
- управляет асинхронными событиями.

После инициализации host создаёт **I/O queues** — собственно очереди для read/write. Их может быть много: обычно одна
SQ+CQ на каждый CPU. Каждая SQ обслуживается своим MSI-X вектором, и interrupt прилетает на «свой» CPU, без межъядерных
пингов.

```
  CPU 0 ──▶ I/O SQ 0 + CQ 0 ──┐
  CPU 1 ──▶ I/O SQ 1 + CQ 1 ──┤
  CPU 2 ──▶ I/O SQ 2 + CQ 2 ──┼──▶ NVMe controller
  CPU 3 ──▶ I/O SQ 3 + CQ 3 ──┤
               ...            │
                              │
  все ядра ─▶ Admin SQ + CQ  ─┘  (один на контроллер)
```

Архитектурно это то, что блочный layer Linux умел уже давно через blk-mq (multi-queue): software queue на ядро
оборачивается в hardware queue, которая 1-в-1 ложится на NVMe SQ. Совпадение не случайно — blk-mq был спроектирован
Jens Axboe именно под NVMe.

## Структура NVMe command

64-байтовая команда в SQ — это `nvme_command` структура. Поля интерпретируются по `opcode`.

| Поле           | Размер   | Что значит                                       |
|----------------|----------|--------------------------------------------------|
| `opcode`       | 1 байт   | `0x00` FLUSH, `0x01` WRITE, `0x02` READ, …       |
| `flags`        | 1 байт   | fused, PRP/SGL selector                          |
| `command_id`   | 2 байта  | tag, host сопоставляет с completion              |
| `nsid`         | 4 байта  | namespace ID                                     |
| `metadata_ptr` | 8 байт   | указатель на metadata (если включена)            |
| `data_ptr`     | 16 байт  | PRP1 + PRP2 или SGL descriptor                   |
| `cmd_dw10..15` | 24 байта | opcode-специфичные параметры (LBA, длина, флаги) |

Для READ/WRITE параметры в `cmd_dw10..15` это: стартовый LBA (8 байт), число логических блоков, флаги защиты данных.
**Data pointer** — описание буфера в host RAM, куда controller должен DMA'нуть данные. Есть два формата:

- **PRP** (Physical Region Pages) — два указателя на физические страницы. Простой, но требует чтобы данные лежали в
  страницах единого размера.
- **SGL** (Scatter Gather List) — общий scatter-gather с произвольной геометрией. Поддерживается опционально.

Большинство Linux-драйверов используют PRP — это исторически проще и поддерживается всеми устройствами.

## NVMe over Fabrics (NVMe-oF)

Локальный NVMe — это transport поверх PCIe. **NVMe over Fabrics** — тот же протокол команд, но transport заменён на
сетевой. Цель: получить производительность NVMe для удалённого устройства, чтобы можно было строить **disaggregated
storage** — отделить compute от storage без потери latency.

| Transport    | Что использует                  | Латентность доп. | Применение                  |
|--------------|---------------------------------|------------------|-----------------------------|
| NVMe/RDMA    | RDMA (RoCE, iWARP, InfiniBand)  | +5–10 µs         | HPC, hyper-scale storage    |
| NVMe/TCP     | обычный TCP/IP                  | +20–50 µs        | commodity Ethernet networks |
| NVMe/FC      | Fibre Channel                   | +10–20 µs        | enterprise SAN              |

Структура командного слоя идентична локальному NVMe — это та же 64-байтовая команда, тот же набор opcode'ов, те же
namespace'ы. Меняется только то, как байты команды доезжают до controller'а: вместо PCIe DMA — RDMA WRITE или TCP send.

В Linux kernel есть modules:

- `nvme-tcp` — TCP transport (host side).
- `nvme-rdma` — RDMA transport.
- `nvme-fc` — Fibre Channel.
- `nvmet` + `nvmet-tcp`/`nvmet-rdma` — target side (Linux может работать как NVMe-oF target, экспортируя локальные
  устройства или файлы).

```bash
# Подключиться к удалённой NVMe-oF target по TCP
nvme connect -t tcp -a 10.0.0.5 -s 4420 -n nqn.2024-01.example:target1
# Появляется /dev/nvme1n1, как обычный локальный namespace
```

NVMe-oF используется в больших облаках под капотом — например, EBS у AWS, persistent disks у GCP. Compute-нода видит
«локальный диск», физически устройство стоит в другой стойке.

## ZNS (Zoned Namespaces)

NAND flash имеет неприятное свойство: писать можно по страницам (4–16 KB), а стирать — только большими блоками (по
несколько MB). Контроллер SSD скрывает это, ведя FTL (Flash Translation Layer), который перемещает данные и собирает
garbage. Цена — **Write Amplification Factor** (WAF): на 1 байт записи от хоста физически пишется 2–5 байт.

**ZNS** (Zoned Namespaces, NVMe 2.0) выносит управление зонной структурой наружу. Namespace делится на **zones** —
непрерывные диапазоны LBA, в которые можно писать только последовательно. Чтобы перезаписать данные в zone, её нужно
явно сбросить (`Zone Reset`). Это похоже на SMR-HDD, только на flash.

```
 Обычный namespace                ZNS namespace
 ┌────────────────────────┐       ┌──────────┬──────────┬──────────┬──────────┐
 │ LBA 0  1  2 ...  N     │       │ Zone 0   │ Zone 1   │ Zone 2   │ Zone N   │
 │ запись в любом порядке │       │ ───▶     │ ───▶     │ ───▶     │ ───▶     │
 └────────────────────────┘       │ wp →     │ wp →     │ full     │ empty    │
                                  │ append   │ append   │ reset()  │ append   │
                                  │ only     │ only     │ to reuse │ only     │
                                  └──────────┴──────────┴──────────┴──────────┘
  FTL внутри устройства            FTL минимален, host управляет
  WAF ~ 2..5                       WAF близок к 1.0
```

Преимущества:

- **WAF близок к 1.0** — host пишет последовательно, контроллеру не нужно собирать мусор.
- **Меньше DRAM в SSD** — нет огромного mapping table; экономия в долларах на enterprise-устройствах.
- **Предсказуемая латентность** — нет внезапных пауз на GC.

Применение — log-structured workloads, которые и так пишут последовательно: RocksDB (есть бэкенд `zenfs`), F2FS (есть
ZNS-mode), object storage с append-only слоем. Linux поддерживает ZNS через `zonefs`, `blk-zoned` API, `nvme zns`
команды `nvme-cli`.

## Linux NVMe stack

Драйвер NVMe в ядре Linux лежит в `drivers/nvme/host/`:

- `pci.c` — PCIe transport (`nvme` модуль для локальных устройств).
- `tcp.c`, `rdma.c`, `fc.c` — fabric transports.
- `core.c` — общий код (admin queue, identify, namespace discovery).

Каждый namespace регистрируется как блочное устройство и подключается к **blk-mq** напрямую. Software queue на CPU
ложится в hardware queue, которая физически соответствует NVMe I/O queue. Никакого scheduler'а по умолчанию (`none`) —
на NVMe полезного reordering нет, а накладные расходы есть.

```
 user-space                                         kernel
 ┌─────────────────┐
 │  write(fd, ...) │
 └────────┬────────┘
          ▼
 ┌─────────────────────────────────────────────────────────────┐
 │  VFS → ext4 → page cache → submit_bio()                     │
 └────────┬────────────────────────────────────────────────────┘
          ▼
 ┌─────────────────────────────────────────────────────────────┐
 │  blk-mq                                                     │
 │  software queue (per-CPU)                                   │
 │       ↓                                                     │
 │  hardware queue (= NVMe SQ)                                 │
 └────────┬────────────────────────────────────────────────────┘
          ▼
 ┌─────────────────────────────────────────────────────────────┐
 │  drivers/nvme/host/pci.c                                    │
 │  build nvme_command(opcode=WRITE, LBA=..., PRP=...)         │
 │  doorbell write (MMIO)                                      │
 └────────┬────────────────────────────────────────────────────┘
          ▼
      PCIe DMA + MSI-X
          ▼
 ┌─────────────────────────────────────────────────────────────┐
 │  NVMe controller                                            │
 │  читает команду, обращается к flash, DMA результат          │
 │  пишет completion → MSI-X interrupt                         │
 └─────────────────────────────────────────────────────────────┘
```

Управление и диагностика — пакет `nvme-cli`:

```bash
nvme list                            # все NVMe устройства и namespace'ы
nvme id-ctrl /dev/nvme0              # информация о контроллере
nvme id-ns  /dev/nvme0n1             # информация о namespace
nvme smart-log /dev/nvme0            # SMART: температура, изношенность, ошибки
nvme format /dev/nvme0n1 -b 4096     # переформатировать (LBA size = 4 KB)
nvme zns report-zones /dev/nvme0n1   # для ZNS namespace'ов
```

## SPDK: kernel bypass

Ядерный путь — это контекстный switch на syscall, копирование struct, блокировки blk-mq (даже если они per-CPU), interrupt
handling. На медленных устройствах эти µs незаметны. На NVMe, где сама команда исполняется за 10 µs, ядерный overhead
становится сопоставим с временем исполнения. И главное — при миллионах IOPS interrupt rate убивает CPU: каждый interrupt
это сохранение контекста, переключение в kernel mode, обработка handler'а, возврат.

**SPDK** (Storage Performance Development Kit, Intel, 2015) — userspace polled-mode driver для NVMe. Идея позаимствована
у DPDK (network kernel bypass): отбираем устройство у ядра через VFIO/UIO, мапим PCIe BAR'ы в userspace, и крутим
busy-loop, опрашивая CQ напрямую. Никаких syscall'ов, никаких прерываний.

```
  Kernel path                                  SPDK (userspace) path
  ┌──────────────────┐                         ┌──────────────────┐
  │  application     │                         │  application     │
  └────────┬─────────┘                         │  + SPDK lib      │
           │ syscall                           └────────┬─────────┘
           ▼                                            │ функция, в том же
  ┌──────────────────┐                                  │ адресном пространстве
  │  VFS / blk-mq    │                                  │
  └────────┬─────────┘                                  ▼
           ▼                                   ┌──────────────────┐
  ┌──────────────────┐                         │ SPDK nvme driver │
  │  kernel nvme drv │                         │ (poll mode)      │
  └────────┬─────────┘                         └────────┬─────────┘
           ▼                                            │ MMIO + DMA
       MMIO + DMA                                       ▼
           ▼                                   ┌──────────────────┐
  ┌──────────────────┐                         │ NVMe controller  │
  │ NVMe controller  │                         │  (тот же)        │
  └──────────────────┘                         └──────────────────┘
           │
       MSI-X interrupt                          completion polling
           ▼                                    в while(1)
  ┌──────────────────┐
  │  IRQ handler     │
  │  + scheduler     │
  │  + context switch│
  └──────────────────┘
```

Каждый поток SPDK (в их терминологии — **reactor**) привязан к ядру (CPU pinning) и крутится в бесконечном цикле:

```c
while (1) {
    spdk_nvme_qpair_process_completions(qpair, 0);
    poll_other_things();
}
```

Если есть completion в CQ — обработали, вызвали callback. Если нет — ушли на следующую итерацию. Никаких блокировок и
ожиданий: latency ровно равна задержке устройства плюс пара десятков наносекунд на обработку.

### Производительность

|                              | Kernel path        | SPDK            | io_uring (IOPOLL)  |
|------------------------------|--------------------|-----------------|--------------------|
| IOPS (4K rand read, 1 core)  | ~1M                | ~10M            | ~5–10M             |
| Latency p50                  | ~30 µs             | ~5 µs           | ~6–8 µs            |
| Latency p99                  | ~80 µs             | ~10 µs          | ~15 µs             |
| CPU при простое              | ~0% (interrupts)   | 100% (polling)  | 100% если IOPOLL   |
| Совместное использование     | нативно            | нужен SR-IOV    | нативно            |
| Code path сложность          | вся kernel stack   | userspace lib   | userspace + kernel |

Главный trade-off: SPDK даёт сверхнизкую латентность ценой постоянной загрузки CPU. Это устройство снимается с ядра
**полностью** — никакое другое приложение не может к нему обратиться через обычный `/dev/nvme0n1`. Multi-tenant требует
**SR-IOV** (Single Root I/O Virtualization) — аппаратной возможности контроллера представляться как несколько virtual
function'ов, каждый из которых можно отдать своему процессу или VM.

Use case'ы:

- **Высокопроизводительные базы данных**: Aerospike, ScyllaDB используют SPDK напрямую.
- **Backend хранилища**: Ceph BlueStore имеет SPDK-режим.
- **VM hypervisor I/O**: SPDK предоставляет `vhost-blk`/`vhost-scsi` backend для QEMU, выставляющий гостям виртуальные
  диски с производительностью почти как у bare-metal.
- **Storage appliances**: проприетарные системы хранения у VAST Data, Lightbits.

### Архитектура SPDK

SPDK построен как фреймворк, не как один драйвер.

```
 ┌─────────────────────────────────────────────────────────┐
 │                    application                          │
 └─────────────────────────────────────────────────────────┘
                              │
 ┌─────────────────────────────────────────────────────────┐
 │              bdev abstraction layer                     │
 │   (унифицированный API для блочных устройств)           │
 └────────┬────────────┬────────────┬──────────────────────┘
          ▼            ▼            ▼
 ┌──────────────┐ ┌──────────┐ ┌──────────────┐
 │  bdev_nvme   │ │  bdev_   │ │  bdev_aio    │
 │  (local PCIe │ │  malloc  │ │  (kernel as  │
 │   или        │ │  RAM disk│ │   fallback)  │
 │   NVMe-oF)   │ │          │ │              │
 └──────┬───────┘ └──────────┘ └──────────────┘
        ▼
 ┌──────────────────────────────────────────────────────────┐
 │                 NVMe driver (userspace)                  │
 │   poll-mode, lockless, per-thread qpairs                 │
 └──────────────────────────────────────────────────────────┘
                              │
 ┌──────────────────────────────────────────────────────────┐
 │   DPDK EAL (Environment Abstraction Layer)               │
 │   hugepages, CPU pinning, lockless rings                 │
 └──────────────────────────────────────────────────────────┘
```

Ключевые элементы:

- **Reactor model**: один поток на ядро, обрабатывает события в event loop. Ни одного блокирующего вызова.
- **Lockless rings** для передачи сообщений между reactor'ами (как в DPDK). Если bdev на одном ядре, а запрос пришёл на
  другое, lockless ring передаёт I/O без mutex'ов.
- **Hugepages** для DMA-буферов (через DPDK EAL). Это уменьшает TLB miss'ы и фрагментацию.
- **bdev abstraction** — слой выше драйверов, экспортирующий унифицированный API. Поверх bdev строятся target'ы:
  iSCSI/NVMe-oF target, vhost-blk, blobfs.

## io_uring + NVMe: альтернатива SPDK

io_uring — relatively new (5.1+) механизм асинхронного I/O в Linux, спроектированный Jens Axboe. С точки зрения NVMe он
интересен тем, что предлагает близкую к SPDK производительность без отказа от ядра.

```c
// io_uring с polling в kernel — низкая латентность, multi-tenant
struct io_uring ring;
io_uring_queue_init(256, &ring, IORING_SETUP_IOPOLL);
```

`IORING_SETUP_IOPOLL` — флаг, заставляющий kernel polling completion напрямую из NVMe CQ, без interrupt'ов. Бонусом
доступен `IORING_SETUP_SQPOLL` — отдельный kernel thread, который polling SQ от userspace, так что приложение даже не
делает syscall для отправки команд.

Сравнение:

|                              | SPDK                  | io_uring (IOPOLL)         |
|------------------------------|-----------------------|---------------------------|
| Где работает poll-loop       | userspace             | kernel thread             |
| Multi-tenant                 | требует SR-IOV        | нативно (kernel арбитрит) |
| Доступ к файловой системе    | нет (raw block)       | да (любой fd)             |
| Сложность интеграции         | переписать приложение | API near POSIX            |
| Производительность           | максимум              | 80–90% от SPDK            |
| Зависимость от kernel версии | минимальная (VFIO)    | требует 5.1+ для базы     |

io_uring выигрывает там, где важна простота интеграции и multi-tenancy: сохраняется обычная файловая система, обычный
`open(2)`, обычные права доступа. SPDK выигрывает там, где latency и IOPS важнее всего остального, а устройство можно
отдать целиком.

Для большинства новых проектов сейчас разумнее начинать с io_uring и переходить на SPDK только когда профилирование
показывает, что упёрлись именно в kernel.

## Диагностика NVMe

Базовая информация:

```bash
nvme list                                    # все устройства и namespace'ы
lspci -vv -s $(nvme list-subsys | grep ...) # PCIe-инфо, link speed
cat /sys/block/nvme0n1/queue/scheduler       # обычно [none]
cat /sys/class/nvme/nvme0/cntrltype          # тип контроллера
```

Производительность — fio:

```bash
# 4K random read, queue depth 32, 1 job
fio --name=randread --filename=/dev/nvme0n1 --rw=randread \
    --bs=4k --iodepth=32 --numjobs=1 --runtime=30 --time_based \
    --ioengine=io_uring --direct=1 --group_reporting
```

Latency-гистограммы:

```bash
biolatency-bpfcc -D 10 1                # гистограмма по устройству
nvme effects-log /dev/nvme0             # эффекты выполненных команд
nvme telemetry-log /dev/nvme0 -o tel.bin # детальный snapshot состояния
```

Низкоуровневая трассировка:

```bash
trace-cmd record -e nvme:* sleep 5      # ftrace events NVMe-драйвера
trace-cmd report
```

## Связанные темы

- [Linux block layer](block_layer.md) — blk-mq, software/hardware queues, scheduler'ы
- [Блочные и символьные устройства](block_and_character_devices.md) — место NVMe в иерархии block devices
- [Файловые дескрипторы](file_descriptors.md) — `O_DIRECT`, io_uring как альтернатива блокирующим syscall'ам

## Источники

- [NVM Express Base Specification — nvmexpress.org](https://nvmexpress.org/specifications/)
- [SPDK documentation — spdk.io](https://spdk.io/doc/)
- [Linux NVMe driver — drivers/nvme/host/](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/nvme/host)
- LWN: ["Improving Linux storage with NVM Express"](https://lwn.net/Articles/520254/)
- LWN: ["NVMe over Fabrics"](https://lwn.net/Articles/684238/)
- LWN: ["Zoned namespaces and the block layer"](https://lwn.net/Articles/824423/)
- ["Efficient IO with io_uring" — Jens Axboe](https://kernel.dk/io_uring.pdf)
- `man 1 nvme`, `man 2 io_uring_setup`
