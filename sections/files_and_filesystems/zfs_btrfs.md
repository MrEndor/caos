# ZFS и Btrfs: Copy-on-Write файловые системы

ext4 и XFS — это journal'ные ФС: они быстры, надёжны и предсказуемы, но устроены вокруг идеи **in-place updates**.
Перезаписали блок — он перезаписан, старого больше нет. Журнал нужен, чтобы пережить сбой посередине транзакции, но не
чтобы сохранить «прошлое состояние» файла.

**ZFS** и **Btrfs** — представители другого поколения. Они построены вокруг **Copy-on-Write** (CoW): любое изменение
пишется в новое место, старые блоки остаются нетронутыми до тех пор, пока на них есть ссылка. Это меняет почти всё:
snapshot'ы становятся почти бесплатными, checksums становятся гарантией целостности, RAID становится частью ФС, а не
отдельным слоем под ней.

Эта статья разбирает обе системы: их общую CoW-модель, специфические сущности (vdev, ARC, ZIL у ZFS; subvolume, reflink
у Btrfs), их различия и зачем выбирать одну вместо другой — или вместо ext4.

## Что у них общего

Несмотря на разные истории и разные сообщества, ZFS и Btrfs решают одну задачу одними средствами.

- **Copy-on-Write**: блоки никогда не перезаписываются. Любое изменение — новый блок, и ссылки на него обновляются вверх
  по дереву (вплоть до суперблока).
- **Integrated volume management**: ФС сама управляет несколькими дисками — RAID/mirror/stripe сделан внутри, без
  отдельного слоя (LVM, mdadm).
- **Snapshots**: моментальный снимок состояния, который занимает почти ноль места, потому что просто «помнит» старый
  корень.
- **Checksumming**: контрольная сумма пишется на каждый блок (данные + метаданные); при чтении проверяется. Если есть
  redundancy (mirror, RAID-Z) — повреждённый блок восстанавливается автоматически.
- **Send/receive**: сериализация snapshot'а в поток байт для бэкапа на удалённый хост, в том числе **incremental** —
  только diff между двумя snapshot'ами.

Обе системы — альтернатива классическим ФС для сценариев, где важна долгосрочная сохранность данных, удобство бэкапа и
устойчивость к bit-rot (постепенное искажение данных на носителе из-за космических лучей, износа flash и пр.).

## Почему именно Copy-on-Write

CoW решает несколько проблем одной механикой.

**Atomic updates.** Старый блок остаётся валидным, пока новый не записан и не зафиксирован в верхних уровнях дерева.
Если питание пропало посередине — на диске последовательное согласованное состояние: либо старая версия, либо новая,
никогда не «полуновая». Journal не нужен.

**Cheap snapshots.** Snapshot — это просто запомнить указатель на текущий корень. Все блоки, на которые он ссылается,
автоматически становятся неперезаписываемыми (refcount растёт). Когда живая ФС изменяет блок — пишется новый блок, старый
остаётся ради snapshot'а. Стоимость snapshot'а в момент создания — O(1), не зависит от размера ФС.

**Bit-rot detection.** Поскольку каждый блок CoW-пишется один раз, на нём можно посчитать checksum при записи и сохранить
её в родительском узле. При чтении checksum пересчитывается и сравнивается. Если данные молча испортились на диске —
проверка покажет mismatch, и при наличии redundancy блок будет восстановлен из mirror/parity.

**Easy clones.** Клон — это writable snapshot. Создаётся бесплатно: общий с источником набор блоков, при записи в клон
ответвляется новая ветка. Используется для VM-образов, контейнерных слоёв, тестовых баз данных.

### Как CoW обновляет дерево

```
 До изменения блока B:
                            ┌─────────┐
                            │  root   │
                            └────┬────┘
                       ┌─────────┼─────────┐
                       ▼         ▼         ▼
                    ┌─────┐   ┌─────┐   ┌─────┐
                    │  A  │   │  B  │   │  C  │
                    └─────┘   └─────┘   └─────┘

 Изменили B → B':
                            ┌──────────┐
                            │  root'   │       ← новый корень,
                            └────┬─────┘         старый ещё валиден
                       ┌─────────┼─────────┐
                       ▼         ▼         ▼
                    ┌─────┐   ┌─────┐   ┌─────┐
                    │  A  │   │  B' │   │  C  │
                    └─────┘   └─────┘   └─────┘
                       ▲                    ▲
                       │                    │
                    общие блоки с прежним root
```

Цена — **write amplification на дереве**: изменение одного байта в листе теоретически требует переписать все узлы по
пути до корня. На практике обе ФС батчат изменения и пишут целыми транзакциями (на ZFS — каждые 5 с по умолчанию),
амортизируя этот overhead.

## ZFS

ZFS появилась в Sun Solaris (2005), затем была портирована на FreeBSD и Linux (через проект OpenZFS). В mainline-ядро
Linux она не входит до сих пор — лицензия CDDL несовместима с GPL, и Linus отказывается её мержить из принципиальных
соображений. На Linux ставится как DKMS-модуль.

### Иерархия: vdev, pool, dataset

ZFS строит свою иерархию из трёх уровней.

```
 ┌──────────────────────────────────────────────────────────────────┐
 │  zpool "tank"                                                    │
 │  ┌──────────────────────────┐  ┌──────────────────────────┐      │
 │  │   vdev: mirror           │  │   vdev: raidz1           │      │
 │  │   ┌──────┐  ┌──────┐     │  │   ┌──────┐ ┌──────┐ ┌──┐ │      │
 │  │   │ sda  │  │ sdb  │     │  │   │ sdc  │ │ sdd  │ │..│ │      │
 │  │   └──────┘  └──────┘     │  │   └──────┘ └──────┘ └──┘ │      │
 │  └──────────────────────────┘  └──────────────────────────┘      │
 │  ┌─────────────────────────────────────────────────────────────┐ │
 │  │   datasets (filesystems и volumes внутри пула)              │ │
 │  │     tank/home                                               │ │
 │  │     tank/home/alice                                         │ │
 │  │     tank/home/bob                                           │ │
 │  │     tank/vm/db1   (zvol — block device)                     │ │
 │  └─────────────────────────────────────────────────────────────┘ │
 └──────────────────────────────────────────────────────────────────┘
```

**vdev** (virtual device) — единица отказоустойчивости. Может быть:

- single disk — без избыточности;
- mirror (RAID 1) — 2+ дисков, читаем с любого, пишем на все;
- raidz1/raidz2/raidz3 — аналог RAID 5/6/7 без «write hole»;
- spare — горячий резерв;
- log (SLOG) — отдельное устройство под ZIL;
- cache (L2ARC) — отдельное устройство под кеш чтения.

**zpool** — пул, объединяющий vdev'ы. Запись в пул автоматически stripes между vdev'ами (RAID 0 поверх vdev). Это
означает: чтобы добавить пулу ёмкости, добавляют **новый vdev**, а не диск в существующий vdev. Внутри vdev'а ZFS не
умеет расти (есть исключение для raidz expansion, появившееся в 2023).

**dataset** — независимая «файловая система» внутри пула. У каждого dataset свои свойства: compression, encryption,
quota, mount point. Их можно вкладывать друг в друга, наследуя свойства. **zvol** — особый тип dataset, представляющий
block device (полезно для VM disk image или iSCSI export).

### ARC и L2ARC

**ARC** (Adaptive Replacement Cache) — кеш чтения ZFS, живёт в RAM. От обычного Linux page cache отличается двумя вещами:

- **Adaptive Replacement** алгоритм — гибрид LRU и LFU. Кеш разделён на два списка: «recently used» и «frequently used»,
  каждый со своим ghost-листом (метаданные выселенных страниц). По соотношению hit'ов в ghost-листах ARC динамически
  меняет соотношение recency vs frequency. На смешанных нагрузках выигрывает у чистого LRU.
- **Управляется самой ZFS**, не page cache ядра. Это даёт ZFS точный контроль над тем, что кешировать (метаданные vs
  данные, разные dataset'ы), но отъедает RAM, которой ОС не может распоряжаться напрямую. Отсюда и слава: ZFS «съедает
  всю RAM».

**L2ARC** — расширение ARC на SSD. Когда блок выселяется из ARC, его копия пишется на L2ARC-устройство. Чтение
неактуального для ARC, но «тёплого» блока — с SSD, не с основного пула. Не путать с reading cache хоть какого-то другого
типа: L2ARC хранит только данные, индекс лежит в RAM (≈70 байт на блок), так что L2ARC «съедает» немного ARC.

### ZIL и SLOG

**ZIL** (ZFS Intent Log) — лог намерений для синхронных операций. По умолчанию живёт внутри пула.

Когда приложение делает синхронную запись (`O_SYNC`, `fsync`), ZFS обязан гарантировать, что данные на диске прежде чем
вернуть управление. Без ZIL это означало бы flush всего transaction group — дорого. Вместо этого ZFS пишет компактную
запись в ZIL (только то, что надо для воспроизведения операции при сбое) и возвращает sync сразу. Данные позже попадут
на диск в обычном transaction group; ZIL читается только при восстановлении после сбоя.

**SLOG** (Separate Log) — выделенное устройство для ZIL, обычно быстрый low-latency SSD (Intel Optane был эталоном).
Драматически ускоряет sync-нагрузки (NFS, базы данных). Если SLOG отказал — ZFS просто пишет ZIL обратно в pool; данные
не теряются.

```
 sync write от приложения
            │
            ▼
 ┌──────────────────────────────────────────┐
 │  ZIL запись на SLOG (если есть)          │  ← sync возвращается ЗДЕСЬ
 │  быстрая, latency ~ ms                   │
 └──────────────────────────────────────────┘
            │
            ▼  (асинхронно, в составе txg)
 ┌──────────────────────────────────────────┐
 │  pool: основное хранилище                │
 │  данные пишутся CoW в текущем txg        │
 └──────────────────────────────────────────┘
            │
            ▼  каждые 5 секунд
 ┌──────────────────────────────────────────┐
 │  txg commit: обновить uberblock          │  ← теперь данные «официально» на диске
 └──────────────────────────────────────────┘
```

### RAID-Z и write hole

Классический RAID 5/6 страдает от **write hole**: если питание пропало после записи данных, но до записи parity (или
наоборот), массив остался в несогласованном состоянии. Восстановить корректность нельзя — неизвестно, где данные правильны,
а где нет. Поэтому продакшен-конфигурации RAID 5/6 требуют либо battery-backed контроллер, либо UPS, либо принимают риск.

**RAID-Z** (и Z2, Z3) использует переменную ширину страйпа: каждая запись формирует свой stripe, который пишется
атомарно как часть транзакции ZFS. Если транзакция не закоммитилась — её просто нет в uberblock'е, и неполные данные на
диске игнорируются. Write hole отсутствует by design.

Цена — RAID-Z не позволяет в общем случае добавить диск в существующий vdev (расширение появилось как experimental в
2023), и random read с RAID-Z дороже, чем с обычного RAID-5 (каждое чтение требует прочитать весь stripe для проверки
checksum).

### send/receive

```bash
# Создать snapshot
zfs snapshot tank/data@hourly1

# Послать его на удалённый хост
zfs send tank/data@hourly1 | ssh backup zfs receive backup_pool/data

# Через час — следующий snapshot и incremental send
zfs snapshot tank/data@hourly2
zfs send -i @hourly1 tank/data@hourly2 | ssh backup zfs receive backup_pool/data
#       ^^^^^^^^^^^  только diff между двумя snapshot'ами
```

Поток `zfs send` — сериализация blocks с метаданными. На приёмнике `zfs receive` восстанавливает структуру. Incremental
send передаёт только блоки, изменившиеся между snapshot'ами — основа всех serious-backup решений на ZFS (zrepl, sanoid,
syncoid).

### Encryption, compression, dedup

- **Encryption** — нативно в ZFS, на уровне dataset. Ключ может быть отдельный для каждого dataset, можно делать
  raw send (зашифрованных блоков) без расшифровки на промежуточных хостах.
- **Compression** — на уровне dataset. Алгоритмы: `lz4` (default; почти бесплатный CPU, обычно ускоряет I/O), `zstd`
  (более сильный, чуть дороже), `gzip-9` (максимум, медленно).
- **Dedup** — inline, на уровне блока, по checksum. Требует огромную dedup table в RAM (≈320 байт на уникальный блок;
  для 10 TB пула с 128 KB-блоками это ≈25 GB). Используется редко из-за стоимости.

### Scrub

`zpool scrub` — фоновое чтение всех блоков с проверкой checksum. Найденные mismatch'и автоматически исправляются из
mirror/parity. Рекомендуется раз в месяц для бытовых пулов, раз в неделю для критичных. Это первичный механизм защиты
от bit-rot.

## Btrfs

Btrfs (B-Tree File System, «butter FS») разрабатывалась Oracle с 2007 года как ответ ZFS, но под GPL и с интеграцией в
mainline-ядро Linux (с 2.6.29, 2009). Архитектурно проще ZFS — нет отдельного volume manager'а, всё построено вокруг
одного дерева B+ trees.

### Subvolume и snapshot

Главная сущность — **subvolume**: namespace внутри ФС, который можно отдельно монтировать, отдельно snapshot'ить,
отдельно `chattr +C`-настраивать.

```
 файловая система /dev/sda1
 ┌────────────────────────────────────────────────┐
 │  root subvolume (id=5)                         │
 │  └── @                                         │
 │      ├── home/                                 │
 │      ├── etc/                                  │
 │      └── ...                                   │
 │                                                │
 │  subvolume @home (id=256)                      │
 │  └── alice/                                    │
 │      bob/                                      │
 │                                                │
 │  subvolume @snapshots (id=257)                 │
 │  ├── @-2024-01-01     ← snapshot @             │
 │  ├── @-2024-01-02                              │
 │  └── @home-2024-01-01 ← snapshot @home         │
 └────────────────────────────────────────────────┘
```

В Linux на Btrfs принято делать корневой раздел `/` отдельным subvolume (например, `@`), а `/home` — другим (`@home`).
Тогда `snapshot @` сохраняет систему, не задевая `/home`. Это базовый паттерн `timeshift`, snapper'а и других tools.

**Snapshot** — read-only или writable копия subvolume. Внутри это просто новая запись в дереве subvolume'ов, ссылающаяся
на те же extent'ы. Стоимость создания — O(1).

```bash
btrfs subvolume create /mnt/@home
btrfs subvolume snapshot -r /mnt/@home /mnt/@snapshots/home-$(date +%F)
btrfs subvolume list /mnt
btrfs subvolume delete /mnt/@snapshots/home-2024-01-01
```

### RAID в Btrfs

Btrfs делает RAID на уровне chunk'ов (≈1 GB), а не дисков. Это даёт гибкость:

- RAID 0, 1, 10 — production-ready, активно используются.
- RAID 5, 6 — реализованы, но имеют **write hole** (как классический RAID, в отличие от ZFS RAID-Z). Не считаются
  production-ready по сей день, документация прямо предупреждает использовать только для тестов.

Эта дыра — основная причина, по которой Btrfs не вытеснил ZFS в storage-серверах.

```bash
# Создать ФС с двумя дисками, RAID 1 для данных и метаданных
mkfs.btrfs -d raid1 -m raid1 /dev/sdb /dev/sdc

# Добавить диск к существующей ФС
btrfs device add /dev/sdd /mnt
btrfs balance start -dconvert=raid1 /mnt   # переразложить с учётом нового диска
```

**balance** — операция переразложения chunks (по дискам, по profile'ам). Используется при добавлении/удалении дисков,
смене RAID profile, восстановлении после деградации. Дорого, но онлайн.

### Reflinks

Поскольку Btrfs CoW, копирование файла можно делать без копирования данных — просто два inode ссылаются на одни и те же
extent'ы. Это **reflink**:

```bash
cp --reflink=always largefile.bin copy.bin    # мгновенно
```

После reflink файлы независимы: изменение одного не задевает другой (CoW сделает новый extent). Это используется
системами контейнеров (Podman/Docker overlay), VM-менеджерами (libvirt qcow2 backing chains можно заменить reflink'ами).

### Compression, scrub, autodefrag

- **Compression**: `compress=zstd:3` (zstd с уровнем 3 — практический баланс), `compress=lzo`, `compress=zlib`. Уровень
  гранулярности — extent, ставится через mount option или `chattr +c`.
- **Scrub**: `btrfs scrub start /mnt` — то же, что в ZFS. Проверка checksum всех блоков, ремонт через mirror.
- **Autodefrag**: mount option, фоновая дефрагментация. Помогает с CoW-фрагментацией базы данных или VM-образов; иначе
  файлы, в которые много раз писали по случайным offset'ам, расползаются на тысячи мелких extent'ов.

Для файлов, плохо переносящих CoW (sqlite, VM image, qcow2), есть флаг `chattr +C` (NOCOW) — отключает CoW для конкретного
файла. Цена — теряется checksum и snapshot не сохранит старые данные этого файла полностью.

### send/receive в Btrfs

Тот же концептуально механизм:

```bash
btrfs subvolume snapshot -r /mnt/@home /mnt/@snap-1
btrfs send /mnt/@snap-1 | ssh backup btrfs receive /backup/

# Incremental
btrfs subvolume snapshot -r /mnt/@home /mnt/@snap-2
btrfs send -p /mnt/@snap-1 /mnt/@snap-2 | ssh backup btrfs receive /backup/
```

## Сравнение ZFS и Btrfs

|                          | ZFS                                    | Btrfs                                  |
|--------------------------|----------------------------------------|----------------------------------------|
| Лицензия                 | CDDL (несовместима с GPL)              | GPL                                    |
| В mainline Linux         | нет (DKMS из OpenZFS)                  | да, с 2.6.29                           |
| Origin                   | Sun Solaris (2005)                     | Oracle Linux (2007)                    |
| RAID 5/6                 | production (RAID-Z, без write hole)    | НЕ production (write hole)             |
| RAID 0/1/10              | mirror, stripe                         | да, на уровне chunks                   |
| Изменение профиля RAID   | ограничено                             | онлайн через `balance`                 |
| Volume management        | встроенный (zpool, vdev)               | встроенный (multi-device support)      |
| Snapshot гранулярность   | per-dataset                            | per-subvolume                          |
| RAM-требования           | высокие (1 GB / 1 TB pool рек.)        | стандартные                            |
| Кеш чтения               | ARC + L2ARC (свой)                     | штатный page cache Linux               |
| Sync-write оптимизация   | ZIL + SLOG                             | journal (commit интервал mount option) |
| Encryption               | native (per-dataset)                   | через LUKS снизу (не FS-level)         |
| Compression              | per-dataset (lz4, zstd, gzip)          | per-extent (zstd, lzo, zlib)           |
| Dedup                    | inline (дорого по RAM)                 | offline (`duperemove`)                 |
| send/receive             | да, incremental                        | да, incremental                        |
| Зрелость на Linux        | очень высокая (через OpenZFS)          | высокая, но RAID 5/6 проблематична     |
| Зрелость на других OS    | FreeBSD, illumos, macOS                | только Linux                           |
| Используется в           | TrueNAS, Proxmox, Netflix Open Connect | Synology, Facebook, Fedora (default)   |

## Когда что выбирать

**ZFS** — если важна максимальная надёжность хранения и удобство operate как «storage appliance»:

- Файловые серверы, NAS, бэкапы.
- RAID 5/6-эквивалент на нескольких дисках.
- Большой объём RAM в наличии (или есть бюджет купить).
- Готовность работать с DKMS-модулем на Linux (либо переход на TrueNAS/Proxmox, где ZFS из коробки).

**Btrfs** — если хочется CoW и snapshot'ы на обычном Linux-десктопе или однодисковом сервере:

- Корневая ФС с автоматическими snapshot'ами перед обновлениями (snapper в openSUSE, timeshift в Linux Mint).
- Один или два диска (mirror), без RAID 5/6.
- Mainline-ядро, никаких внешних модулей.
- Контейнерные хосты (Podman, Docker), где reflinks дают почти бесплатные слои.

**ext4 / XFS** — если CoW не нужен и важна предсказуемая bare-metal производительность:

- Базы данных, которые сами управляют целостностью (PostgreSQL, MySQL); CoW дублирует их работу и фрагментирует.
- Сценарии, где нет потребности в snapshot'ах или дедупликации.
- Минимальные требования к RAM и CPU.

## CoW в действии: как живёт snapshot

Покажем, как Btrfs/ZFS обращаются с разделяемыми блоками при изменении.

```
 Шаг 1. Создан snapshot subvol@T0 от текущего @ subvolume.
        Все блоки разделены: snapshot и live FS указывают на одни и те же extent'ы.

   subvol @                  subvol @T0 (snapshot, read-only)
       │                            │
       ▼                            ▼
   ┌───┴──────┐               ┌─────┴────┐
   │  root    │               │  root    │
   └───┬──────┘               └─────┬────┘
       │                            │
       └──────────┬─────────────────┘
                  ▼
              общие
           metadata + data
           extent'ы A, B, C
```

```
 Шаг 2. Приложение изменило часть файла, лежащего в extent B.
        Btrfs/ZFS CoW: пишется новый extent B', root в live FS обновляется.

   subvol @                   subvol @T0
       │                            │
       ▼                            ▼
   ┌───┴──────┐               ┌─────┴───┐
   │  root'   │               │  root   │
   └────┬─────┘               └────┬────┘
        │                          │
   ┌────┴──┬──────┐            ┌───┴──┬──────┐
   │   A   │  B'  │   C        │   A  │  B   │   C
   └───────┴──────┘            └──────┴──────┘
       │       │                  │      │
       ▼       ▼                  ▼      ▼
     (общий) (новый)            (общий) (старый,
                                         удерживается snapshot'ом)
```

```
 Шаг 3. Snapshot удалён → старый B освобождается (refcount упал до 0).
        Если snapshot нужен — занятое им пространство = только B (плюс служебные блоки).
```

Поэтому snapshot, который сделан давно и в котором сохранены давно перезаписанные блоки, может занимать значительный
объём — он эквивалентен diff'у между моментом снимка и текущим состоянием. Старые ненужные snapshot'ы периодически
чистят (`btrfs subvolume delete`, `zfs destroy snap`).

## Диагностика

### ZFS

```bash
zpool status                          # состояние пула, дисков, vdev'ов
zpool iostat -v 1                     # I/O статистика
zpool list -v                         # размер, фрагментация, использование
zfs list -t all                       # все dataset и snapshot
zfs get all tank/data                 # все свойства dataset
arc_summary                           # статистика ARC (RAM cache)
zpool scrub tank                      # запустить scrub
zpool scrub -s tank                   # остановить scrub
```

### Btrfs

```bash
btrfs filesystem show                 # все Btrfs ФС в системе
btrfs filesystem df /mnt              # использование по типам chunk
btrfs filesystem usage /mnt           # детально по дискам
btrfs subvolume list /mnt             # все subvolume и snapshot
btrfs scrub start /mnt                # запустить scrub
btrfs scrub status /mnt               # статус scrub
btrfs balance status /mnt             # статус balance
btrfs device stats /mnt               # статистика ошибок по устройствам
```

## Связанные темы

- [Внутреннее устройство файловых систем](filesystem_internals.md) — устройство ext4, сравнение с CoW-системами
- [Linux block layer](block_layer.md) — путь I/O от файловой системы до диска
- [Жёсткие и символические ссылки](hard_and_symbolic_links.md) — hard link против CoW reflink

## Источники

- [OpenZFS documentation](https://openzfs.github.io/openzfs-docs/)
- [Btrfs wiki — kernel.org](https://btrfs.readthedocs.io/en/latest/)
- ["ZFS: The Last Word in Filesystems" — Bonwick, Moore](https://www.cs.hmc.edu/~rhodes/courses/cs134/spring2018/papers/zfs_last.pdf)
- ["BTRFS: The Linux B-tree Filesystem" — Ohad Rodeh](https://dominoweb.draco.res.ibm.com/reports/rj10501.pdf)
- LWN: ["A short history of btrfs"](https://lwn.net/Articles/342892/)
- LWN: ["Snapshots, hot tubs, and the future of ZFS"](https://lwn.net/Articles/906762/)
- `man 8 zpool`, `man 8 zfs`, `man 8 btrfs`
