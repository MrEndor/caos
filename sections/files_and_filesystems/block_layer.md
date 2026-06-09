# Linux block layer: путь I/O от userspace до диска

Между системным вызовом `write(fd, buf, 4096)` и реальной записью на диск лежит несколько слоёв ядра. Каждый из них
решает свою задачу: VFS приводит вызов к единому виду независимо от файловой системы, page cache откладывает реальную
запись, **block layer** превращает запрос в очередь команд для драйвера, I/O scheduler решает в каком порядке их
отправлять, и только потом драйвер шлёт что-то железу. Понимание этой цепочки нужно для performance-тюнинга (выбор
правильного scheduler), отладки latency (`iostat`, `blktrace`) и осознания, почему `write` возвращается мгновенно, а
данные на диск попадают через секунды.

## Архитектура: где находится block layer

Block layer — это часть ядра, абстрагирующая блочное устройство для всего, что выше. Файловые системы и драйверы видят
не SSD, не SATA, не NVMe — они видят `struct block_device` и работают с ним через bio (block I/O request).

```
  Userspace
  ┌────────────────────────────────────────────────────────────┐
  │   open("/data/log", O_WRONLY)                              │
  │   write(fd, buf, 4096)                                     │
  └────────────────────────────┬───────────────────────────────┘
                               │  syscall
                               ▼
  ┌────────────────────────────────────────────────────────────┐
  │                            VFS                             │
  │   sys_write → vfs_write → file->f_op->write_iter           │
  └────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
  ┌────────────────────────────────────────────────────────────┐
  │                  Filesystem driver (ext4)                  │
  │   Превращает запись в файл в запись блоков на устройстве:  │
  │   - находит block в extent tree                            │
  │   - помечает страницы page cache как dirty                 │
  └────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
  ┌────────────────────────────────────────────────────────────┐
  │                       Page cache                           │
  │   write возвращается сразу.                                │
  │   Реальная отправка — асинхронно через writeback           │
  │   (kernel thread, либо триггер fsync/sync)                 │
  └────────────────────────────┬───────────────────────────────┘
                               │  bio submit
                               ▼
  ┌────────────────────────────────────────────────────────────┐
  │                       Block layer                          │
  │   ┌──────────────┐    ┌──────────────┐                     │
  │   │  bio         │ ─▶ │  request     │                     │
  │   │ (one I/O)    │    │ (агрегация)  │                     │
  │   └──────────────┘    └──────┬───────┘                     │
  │                              ▼                             │
  │   ┌────────────────────────────────────┐                   │
  │   │   request queue (per-device)       │                   │
  │   └──────────────┬─────────────────────┘                   │
  │                  ▼                                         │
  │   ┌────────────────────────────────────┐                   │
  │   │   I/O scheduler                    │                   │
  │   │   (none / mq-deadline / bfq / ...) │                   │
  │   └──────────────┬─────────────────────┘                   │
  └──────────────────┼─────────────────────────────────────────┘
                     ▼
  ┌────────────────────────────────────────────────────────────┐
  │                   Block device driver                      │
  │   nvme / ahci / virtio-blk / scsi / ...                    │
  └────────────────────────────┬───────────────────────────────┘
                               │  DMA / MMIO / PCIe
                               ▼
  ┌────────────────────────────────────────────────────────────┐
  │                        Hardware                            │
  │       SSD / NVMe / HDD / SAN                               │
  └────────────────────────────────────────────────────────────┘
```

Не все запросы проходят через page cache: при `O_DIRECT` файловая система формирует bio напрямую из user-buffer'а,
минуя кеш. Это нужно базам данных, которые хотят сами управлять кешированием.

## bio: block I/O request

**bio** (block I/O) — это описание одной операции ввода-вывода. Структура `struct bio` в ядре содержит:

| Поле          | Назначение                                                         |
|---------------|--------------------------------------------------------------------|
| `bi_bdev`     | целевое блочное устройство (`struct block_device`)                 |
| `bi_iter`     | начальный сектор (`bi_sector`), длина в байтах, текущая позиция    |
| `bi_opf`      | операция: `REQ_OP_READ`, `REQ_OP_WRITE`, `REQ_OP_FLUSH`, `DISCARD` |
| `bi_io_vec[]` | массив `(page, offset, length)` — scatter-gather список страниц    |
| `bi_end_io`   | callback, вызываемый по завершении (success или error)             |
| `bi_status`   | результат: `BLK_STS_OK` или код ошибки                             |

bio описывает физически непрерывный диапазон **на устройстве**, но данные в памяти могут быть рассыпаны — отсюда массив
`bi_io_vec`. Это позволяет одним bio записать 16 страниц page cache, лежащих в RAM в разных местах, в один непрерывный
кусок диска.

```
  Один bio: непрерывный диапазон секторов + scatter list страниц RAM

  bi_sector = 1024,  bi_size = 16 KB,  op = WRITE
  ┌──────────────────────────────────────────────────────────┐
  │ bi_io_vec[0]: page A, offset 0,    length 4096           │
  │ bi_io_vec[1]: page B, offset 0,    length 4096           │
  │ bi_io_vec[2]: page C, offset 2048, length 2048           │
  │ bi_io_vec[3]: page D, offset 0,    length 4096           │
  │ bi_io_vec[4]: page E, offset 0,    length 2048           │
  └──────────────────────────────────────────────────────────┘
            │                              │
            ▼                              ▼
       RAM (страницы)               Диск (сектора 1024..1056)
       [A] [B] [C] [D] [E]          [────────── 16 KB ──────────]
       разбросаны                   один непрерывный диапазон
```

Файловая система создаёт bio и отправляет в block layer через `submit_bio()`. Дальше bio живёт самостоятельно: scheduler
объединяет соседние bio в один request, драйвер забирает request из очереди и выдаёт команду железу. Callback `bi_end_io`
срабатывает в interrupt context, когда устройство сообщает о завершении.

## request и request queue

**request** — это агрегат из одного или нескольких bio, описывающий **одну команду драйверу**. Зачем нужна
дополнительная сущность поверх bio:

1. **Merging.** Если две bio пишут соседние секторы, выгодно объединить их в один request — драйверу одна команда вместо
   двух, диску — одна головка/один SQ entry вместо двух.
2. **Reordering.** Scheduler перетасовывает request'ы (не bio) — оперирует более крупными единицами.
3. **Per-driver state.** request хранит указатели на структуры драйвера, тэг команды, statistics.

**Request queue** (`struct request_queue`) — очередь запросов на одно устройство. У каждого `/dev/sdX`, `/dev/nvmeXnY`
есть своя request queue. В неё попадают request'ы и из неё драйвер забирает их в порядке, который определяет scheduler.

```
  bio submit ─▶ block layer ─▶ попытка merge ─▶ request queue ─▶ scheduler ─▶ driver
                                   │
                                   ▼
                          ┌────────────────────┐
                          │ соседний request   │
                          │ уже в очереди?     │
                          ├────────────────────┤
                          │ да → merge bio в   │
                          │      существующий  │
                          │      request       │
                          │ нет → создать новый│
                          │       request      │
                          └────────────────────┘
```

## Plug/unplug: батчинг

Если поток создаёт много bio подряд, отправлять каждый сразу в scheduler — расход CPU на блокировки очереди. Block layer
использует механизм **plugging**: текущий поток заводит себе локальный список `blk_plug`, в который складируются bio,
и отдаёт всё в очередь одним пакетом при **unplug** — явном вызове, при переключении контекста или по таймауту.

```c
struct blk_plug plug;
blk_start_plug(&plug);
    for (i = 0; i < N; i++)
        submit_bio(bios[i]);    // накапливаются в plug
blk_finish_plug(&plug);          // unplug: всё уходит в request queue
```

Это даёт scheduler'у видеть пакет bio сразу и эффективнее их объединять, а очереди не блокироваться на каждый bio
отдельно.

## I/O schedulers

Scheduler решает, **в каком порядке** request'ы из очереди уйдут драйверу. На HDD выбор порядка критически влиял на
throughput — головка диска должна была перемещаться оптимально (elevator algorithm). На SSD/NVMe порядок почти не
важен для самого носителя, но всё ещё важен для **fairness** и **latency** между процессами.

В Linux 5.0+ остались только multi-queue scheduler'ы (см. blk-mq). Доступны:

| Scheduler     | Логика                                        | Когда выбирать                                  |
|---------------|-----------------------------------------------|-------------------------------------------------|
| `none`        | FIFO, никакого переупорядочения и merging     | NVMe, fast SSD — overhead больше, чем выгода    |
| `mq-deadline` | дедлайны для read/write, гарантии max latency | универсальный default, особенно с задержкой     |
| `bfq`         | Budget Fair Queueing, fair share + приоритеты | desktop, latency-sensitive workload, mixed I/O  |
| `kyber`       | dynamic latency targets, lightweight          | fast SSD/NVMe, когда нужна fair-очередь без bfq |

**none** не делает ничего — просто FIFO. Для NVMe это часто лучший выбор: устройство само имеет внутреннюю очередь
команд (до 64K), параллельно выполняет десятки операций, и любая reordering-логика в ядре только тратит CPU.

**mq-deadline** хранит две очереди (read, write) с временными метками и гарантирует, что request не пролежит дольше
дедлайна (по умолчанию 500 мс для read, 5 с для write). Reads приоритетнее, чтобы один тяжёлый writer не блокировал
интерактивные читатели.

**bfq** (Budget Fair Queueing) выделяет каждому процессу «бюджет» секторов и обслуживает их по очереди, как CFS на CPU.
Поддерживает `ionice`-приоритеты. Платит за это заметным CPU-overhead — на ноутбуке хорошо, на сервере с 1M IOPS уже
становится bottleneck'ом.

**kyber** — упрощённый scheduler с двумя целями: target latency для reads и для sync writes. Подкручивает глубину
очереди (queue depth) под нагрузку, держа latency в заданных рамках.

Текущий scheduler устройства:

```bash
cat /sys/block/sda/queue/scheduler
# [mq-deadline] kyber bfq none
#  ↑ в скобках — активный
```

Сменить:

```bash
echo bfq > /sys/block/sda/queue/scheduler
```

Постоянная установка — через udev rule.

## Multi-queue (blk-mq)

Старая (single-queue) архитектура имела **одну** request queue на устройство, защищённую глобальным spinlock'ом. На HDD
с 100 IOPS это никого не волновало. С приходом NVMe (миллион IOPS на устройство) и многоядерных CPU блокировка стала
главным bottleneck'ом: все ядра дрались за один lock.

Ответ — **blk-mq** (multi-queue block layer), Jens Axboe, Linux 3.13 (2014). Идея: дать каждому CPU свою очередь.

```
  Single-queue (legacy):
                                         ┌────────────────────┐
   CPU0 ──▶ ┌──────────────────┐         │                    │
   CPU1 ──▶ │  request queue   │ ──▶     │      driver        │
   CPU2 ──▶ │   (один lock!)   │         │                    │
   CPU3 ──▶ └──────────────────┘         └────────────────────┘
                       ▲
                   bottleneck

  Multi-queue (blk-mq):
   CPU0 ──▶ [sw queue 0] ─┐
   CPU1 ──▶ [sw queue 1] ─┤    ┌──────────────┐     ┌─────────────┐
   CPU2 ──▶ [sw queue 2] ─┼─▶  │ hw queue 0   │ ──▶ │ NVMe SQ 0   │
   CPU3 ──▶ [sw queue 3] ─┘    ├──────────────┤     ├─────────────┤
                               │ hw queue 1   │ ──▶ │ NVMe SQ 1   │
                               ├──────────────┤     ├─────────────┤
                               │ hw queue N   │ ──▶ │ NVMe SQ N   │
                               └──────────────┘     └─────────────┘
```

blk-mq различает два уровня очередей:

- **software queues** — per-CPU, без блокировок между ядрами. Сюда поток кладёт свои bio.
- **hardware queues** — соответствуют аппаратным очередям устройства. NVMe умеет иметь несколько SQ (submission
  queue), каждая обслуживается отдельным MSI-X interrupt'ом на свой CPU. SCSI/SATA — обычно одна.

На пути sw → hw scheduler уже работает многопоточно. Старые scheduler'ы (`cfq`, `deadline`, `noop`) умерли — они не
умели mq. Заменены на `mq-deadline`, `bfq`, `kyber`, `none`.

## I/O barriers и FUA

Часть данных нужно гарантированно сохранить на диск **до** следующих операций — например, журнал ФС обязан попасть на
диск до записи реальных данных, иначе при сбое журнал станет нерелевантен. Ранние ядра использовали **I/O barriers** —
специальные bio, после которых очередь не отдавала ничего следующего, пока предыдущее не закоммитилось.

Современный Linux заменил barriers на два явных флага операции:

- **`REQ_PREFLUSH`** — перед этой записью выполнить flush write-кеша устройства (всё, что уже отправлено, должно лечь на
  носитель).
- **`REQ_FUA`** (Force Unit Access) — эта конкретная запись должна попасть прямо в энергонезависимое хранилище, минуя
  volatile write cache устройства.

```
  Обычная запись:
     bio ──▶ device write cache (RAM) ──▶ (потом) ──▶ flash
                   ▲
                   └── на этом этапе пропадёт при power loss

  Запись с REQ_FUA:
     bio ──▶ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─▶ flash
                                              ▲
                                              └── вернётся только когда тут

  Запись с REQ_PREFLUSH:
     [flush уже накопленного кеша] ──▶ затем bio как обычно
```

`fsync(fd)` именно так и устроен: файловая система формирует FLUSH+FUA bio, ждёт его completion, и только тогда `fsync`
возвращается пользователю. Драйверы устройств транслируют эти флаги в native-команды: SCSI `SYNCHRONIZE CACHE` + bit
FUA в `WRITE`, NVMe — команды `Flush` и `Write` с `FUA=1`.

## Диагностика

**iostat -x 1** — общая картина утилизации:

```
Device  r/s    w/s    rkB/s   wkB/s   rrqm/s wrqm/s  r_await w_await  %util
nvme0n1 1240   856   158720  109568    0.00   12.40    0.18    0.31   78.5
```

Что смотреть:

- `r/s`, `w/s` — IOPS на чтение и запись.
- `rkB/s`, `wkB/s` — пропускная способность.
- `rrqm/s`, `wrqm/s` — слитые (merged) запросы в секунду; высокое значение — block layer успешно объединяет bio.
- `r_await`, `w_await` — среднее время от подачи request в очередь до завершения, в мс.
- `%util` — процент времени, когда устройство было занято хоть одним запросом. На NVMe с параллелизмом 100%-util ещё
  не значит насыщения.

**blktrace + blkparse** — пер-bio трассировка событий:

```bash
blktrace -d /dev/sda -o trace -w 10    # 10 секунд
blkparse -i trace                       # читабельный вывод
```

Видны события: `Q` (queued), `M` (merged), `D` (dispatched to driver), `C` (completed) — и для каждого таймштамп. Можно
понять, на каком этапе теряется латентность.

**sysfs параметры устройства** — настройки очереди:

```bash
cat /sys/block/sda/queue/scheduler         # текущий scheduler
cat /sys/block/sda/queue/nr_requests       # глубина очереди
cat /sys/block/sda/queue/read_ahead_kb     # размер read-ahead
cat /sys/block/sda/queue/rotational        # 1 = HDD, 0 = SSD/NVMe
cat /sys/block/sda/queue/max_sectors_kb    # макс размер одного request
```

**eBPF / bcc tools** — современные средства:

```bash
biolatency-bpfcc      # гистограмма latency block I/O
biosnoop-bpfcc        # каждый bio с PID, размером, latency
bitesize-bpfcc        # распределение размеров I/O
```

## Связанные темы

- [Основы файловых систем](filesystems_basics.md) — где живут файловая система и VFS на схеме блочного I/O
- [Блочные и символьные устройства](block_and_character_devices.md) — кому block layer служит снизу
- [Файловые дескрипторы](file_descriptors.md) — `O_DIRECT`, `O_SYNC` и их влияние на путь bio
- [mmap и отображение файлов](../memory/mmap_and_file_mapping.md) — как страницы из page cache превращаются в bio при writeback

## Источники

- [block/ — kernel.org Documentation](https://www.kernel.org/doc/Documentation/block/)
- [Linux Storage Stack Diagram — Werner Fischer](https://www.thomas-krenn.com/en/wiki/Linux_Storage_Stack_Diagram)
- LWN: ["The multiqueue block layer"](https://lwn.net/Articles/552904/)
- LWN: ["Block layer introduction part 1: the bio layer"](https://lwn.net/Articles/736534/)
- LWN: ["Explicit block-layer plugging"](https://lwn.net/Articles/438256/)
- `man 1 iostat`, `man 8 blktrace`, `man 8 blkparse`
