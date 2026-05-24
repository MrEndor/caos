# mmap и маппинг файлов

`mmap(2)` — системный вызов для отображения файлов или анонимной памяти в адресное пространство процесса.
Через него работают: загрузка `.so`-библиотек, крупные аллокации `malloc`, файловые БД, разделяемая память.

## Сигнатура

```c
#include <sys/mman.h>

void *mmap(void   *addr,    // желаемый адрес (NULL = ядро выбирает)
           size_t  length,  // размер в байтах
           int     prot,    // PROT_READ|PROT_WRITE|PROT_EXEC|PROT_NONE
           int     flags,   // MAP_SHARED, MAP_PRIVATE, MAP_ANONYMOUS, MAP_FIXED, ...
           int     fd,      // файловый дескриптор (-1 для анонимного)
           off_t   offset); // смещение в файле (кратно странице)

int munmap(void *addr, size_t length);
```

Возвращает указатель на начало региона или `MAP_FAILED` при ошибке.

## Layout виртуальной памяти с mmap-регионами

```
Виртуальное адресное пространство процесса (64-бит Linux, высокие адреса наверху)

┌───────────────────────────────────────────────────┐  0xffff...
│                  kernel space                     │
├───────────────────────────────────────────────────┤  0x7fff...
│  stack  (растёт вниз ↓)                           │
│  [guard page]  ---                                │
├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
│                                                   │
│  ┌─────────────────────────────────────────────┐  │
│  │  MAP_SHARED | MAP_ANONYMOUS                 │  │  ← POSIX shm / fork-shared
│  │  (разделяемая анонимная память)             │  │
│  └─────────────────────────────────────────────┘  │
│                                                   │
│  ┌─────────────────────────────────────────────┐  │
│  │  MAP_PRIVATE | MAP_ANONYMOUS                │  │  ← крупные malloc (≥128 KB)
│  │  (приватная анонимная память)               │  │    через mmap(MAP_ANONYMOUS)
│  └─────────────────────────────────────────────┘  │
│                                                   │
│  ┌─────────────────────────────────────────────┐  │
│  │  MAP_SHARED  file mapping                   │  │  ← IPC, запись в файл,
│  │  data.bin  r/w  offset=0                    │  │    изменения видны всем
│  └─────────────────────────────────────────────┘  │
│                                                   │
│  ┌─────────────────────────────────────────────┐  │
│  │  MAP_PRIVATE file mapping                   │  │  ← загрузка .so: r-x сегмент
│  │  libfoo.so  r-x                             │  │    copy-on-write при записи
│  └─────────────────────────────────────────────┘  │
│                                                   │
├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤  ← program break (brk)
│  heap  (растёт вверх ↑)                           │
├───────────────────────────────────────────────────┤
│  .bss / .data / .text                             │  ← MAP_PRIVATE file mapping
└───────────────────────────────────────────────────┘  0x0000...
```

## Анонимная память

Выделить страницы без привязки к файлу:

```c
void *ptr = mmap(NULL, 4096,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0);
if (ptr == MAP_FAILED) { perror("mmap"); return 1; }

((char *)ptr)[0] = 'A';

munmap(ptr, 4096);
```

## Маппинг файла

```c
int fd = open("data.bin", O_RDWR);
struct stat st;
fstat(fd, &st);

char *data = mmap(NULL, st.st_size,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED,        // изменения попадают в файл
                  fd, 0);

data[0] = 'X';                       // меняем первый байт файла в памяти
msync(data, st.st_size, MS_SYNC);    // гарантированно сбросить на диск

munmap(data, st.st_size);
close(fd);
```

После `mmap` дескриптор `fd` можно закрыть — маппинг остаётся действительным.

## Маппинг файла через page cache

Ядро не копирует данные файла напрямую в процесс — оно создаёт маппинг на страницы **page cache** (единый буфер файловых
данных). Несколько процессов с `MAP_SHARED` одного файла разделяют одни и те же физические страницы:

```
 Файл на диске            Page cache (ядро)              Виртуальная память
 (block device)                                          процессов

 ┌─────────────┐          ┌──────────────────┐
 │  data.bin   │          │ page @ offset 0  │◀──┐  Процесс A (MAP_SHARED)
 │             │  page    │  [физ. стр. 0x1] │   ├──▶ vaddr 0x7f000000
 │  offset  0  │──fault──▶│                  │   │
 │  offset  1  │          ├──────────────────┤   │  Процесс B (MAP_SHARED)
 │  offset  2  │          │ page @ offset 1  │◀──┤──▶ vaddr 0x7f001000
 │  ...        │          │  [физ. стр. 0x2] │   │    (та же физ. страница!)
 └─────────────┘          ├──────────────────┤   │
                          │ page @ offset 2  │   │  Процесс C (MAP_PRIVATE)
                          │  [физ. стр. 0x3] │◀──┘──▶ vaddr 0x7f002000
                          └──────────────────┘            │
                                                          │ первая запись →
                                                          ▼ copy-on-write
                                                    ┌──────────────────┐
                                                    │ COW copy         │
                                                    │ [физ. стр. 0xA]  │  приватная копия
                                                    └──────────────────┘
```

- `MAP_SHARED`: PTE всех процессов указывают на одну физическую страницу page cache. Запись в памяти — запись в файл (
  через page cache, грязная страница).
- `MAP_PRIVATE`: изначально PTE тоже указывает на страницу page cache (read-only). При первой записи ядро создаёт
  приватную копию (COW — copy-on-write). Файл не затрагивается.

## MAP_SHARED vs MAP_PRIVATE

|                                  | MAP_SHARED                | MAP_PRIVATE                     |
|----------------------------------|---------------------------|---------------------------------|
| Изменения видны другим процессам | Да                        | Нет                             |
| Изменения попадают в файл        | Да (через page cache)     | Нет                             |
| Механизм                         | Общая физическая страница | Copy-on-write при первой записи |
| Типичное использование           | IPC, запись в файл        | Загрузка .so, приватные данные  |

## mremap — изменение размера

```c
void *ptr = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

ptr = mremap(ptr, 4096, 8192, MREMAP_MAYMOVE);
// MREMAP_MAYMOVE — разрешить перемещение, если рядом нет места
```

## MAP_FIXED — фиксированный адрес

```c
void *ptr = mmap((void*)0x60000000, 4096,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                 -1, 0);
```

Без `MAP_FIXED`: `addr` — лишь подсказка. С `MAP_FIXED`: ядро обязано, но **может перезатереть** существующий маппинг.

## msync — синхронизация с файлом

```c
int msync(void *addr, size_t length, int flags);
// MS_SYNC  — синхронно ждать записи на диск
// MS_ASYNC — асинхронно
```

Без `msync` ядро сбросит изменения само, но момент не гарантирован.
Для БД и журналирования нужен явный `msync` или `fsync`.

## madvise — подсказки ядру

`madvise(2)` сообщает ядру о предполагаемом паттерне использования региона. Сам по себе вызов не меняет содержимое
памяти и не выделяет/не освобождает физические страницы напрямую — он подсказывает, как ядру выгоднее управлять
readahead, page cache и reclaim.

```c
#include <sys/mman.h>

int madvise(void *addr, size_t length, int advice);
```

| Флаг `advice`            | Поведение ядра                                                                                             |
|--------------------------|------------------------------------------------------------------------------------------------------------|
| `MADV_NORMAL`            | Поведение по умолчанию (умеренный readahead)                                                               |
| `MADV_SEQUENTIAL`        | Последовательный доступ: агрессивнее readahead, страницы за курсором быстрее вытесняются                   |
| `MADV_RANDOM`            | Случайный доступ: readahead отключён, читается ровно запрошенная страница                                  |
| `MADV_WILLNEED`          | Скоро понадобится — preload страниц в page cache (асинхронный readahead)                                   |
| `MADV_DONTNEED`          | Больше не нужно — страницы можно сбросить; для anonymous mapping страницы зануляются при следующем доступе |
| `MADV_FREE` (Linux 4.5+) | Lazy free для anonymous: страницы помечаются «можно забрать», но не отдаются, пока нет memory pressure     |
| `MADV_HUGEPAGE`          | Разрешить/запросить THP для региона (учитывается khugepaged)                                               |
| `MADV_NOHUGEPAGE`        | Запретить THP для региона                                                                                  |
| `MADV_REMOVE`            | Освободить диапазон в tmpfs/shm (как `fallocate(PUNCH_HOLE)`)                                              |

### MADV_SEQUENTIAL и MADV_RANDOM

Влияют на readahead — механизм опережающего чтения файла с диска в page cache. Для сканирования большого лога
последовательно:

```c
char *data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
madvise(data, file_size, MADV_SEQUENTIAL);
// ядро поднимает readahead window, отдаёт страницы раньше, чем процесс к ним доберётся
```

Для случайного индексного доступа `MADV_RANDOM` отключает readahead — иначе ядро будет таскать соседние страницы,
которые не пригодятся.

### MADV_WILLNEED и MADV_DONTNEED

`MADV_WILLNEED` запускает асинхронный readahead — данные подтягиваются в page cache до того, как процесс к ним
обратится. На anonymous mapping действует так же, но смысла у него мало (страниц ещё нет).

`MADV_DONTNEED` ведёт себя по-разному:

```
File mapping (MAP_PRIVATE/MAP_SHARED + fd):
   MADV_DONTNEED → PTE очищается; следующее обращение породит page fault,
                   страница перечитывается из page cache/файла

Anonymous mapping (MAP_ANONYMOUS):
   MADV_DONTNEED → физические страницы возвращаются ядру немедленно;
                   следующий доступ даст СВЕЖУЮ занулённую страницу
                   (любые записанные данные пропадают!)
```

Это используют аллокаторы и сборщики мусора, чтобы вернуть RSS, не разрушая виртуальный регион.

### MADV_FREE — lazy free для аллокаторов

`MADV_FREE` (Linux 4.5+) — компромисс между `MADV_DONTNEED` и удержанием страницы. Ядро помечает страницу как «свободна,
но содержимое валидно»: если до memory pressure процесс снова запишет в неё — страница остаётся как есть, без page fault
и обнуления.

```
              MADV_DONTNEED                    MADV_FREE
              ────────────                     ─────────

free()  ──▶  ядро отбирает PTE,         ──▶  ядро помечает PTE как «lazy free»
             RSS падает сразу                RSS пока не меняется
                                              физ. страница висит в lazy-free pool

малое               page fault                 запись без fault — страница
переиспользование   + зануление страницы       возвращается процессу как есть

memory              —                          ядро забирает страницу,
pressure                                       следующий доступ → fresh zero page
```

Цена: страница либо возвращается мгновенно (быстрее `DONTNEED`), либо вообще не освобождается. Удобно для аллокаторов
вроде jemalloc — там `MADV_FREE` стал дефолтом на Linux. Минус: RSS не падает, пока ядро не решит забрать страницы —
мониторинг показывает «утечку», которой нет.

**Пример: bump-аллокатор с MADV_FREE**

```c
#include <sys/mman.h>
#include <stdint.h>

#define POOL_SIZE (64 * 1024 * 1024)  // 64 MB region

typedef struct {
    uint8_t *base;
    size_t   offset;
} bump_arena;

static int arena_init(bump_arena *a) {
    a->base = mmap(NULL, POOL_SIZE, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (a->base == MAP_FAILED) return -1;
    a->offset = 0;
    return 0;
}

static void *arena_alloc(bump_arena *a, size_t n) {
    void *p = a->base + a->offset;
    a->offset += (n + 15) & ~15;  // align 16
    return a->offset > POOL_SIZE ? NULL : p;
}

// reset = вернуть курсор в 0 и сказать ядру: «эти страницы можно забрать»
static void arena_reset(bump_arena *a) {
    madvise(a->base, a->offset, MADV_FREE);
    a->offset = 0;
}
```

`MADV_FREE` здесь дешевле `munmap`+`mmap` (нет syscall на каждую итерацию, нет повторного создания VMA) и дешевле
`MADV_DONTNEED` под нагрузкой — при наличии свободной памяти страницы переиспользуются без page fault.

### MADV_HUGEPAGE / MADV_NOHUGEPAGE

Используются совместно с системной настройкой `transparent_hugepage/enabled=madvise`: в этом режиме ядро собирает THP
только для регионов, явно помеченных `MADV_HUGEPAGE`.

```c
void *p = mmap(NULL, 2 * 1024 * 1024,
               PROT_READ|PROT_WRITE,
               MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
madvise(p, 2 * 1024 * 1024, MADV_HUGEPAGE);
```

## Huge pages

Стандартная страница на x86-64 — 4 KB. Huge pages — 2 MB и 1 GB. На больших рабочих наборах это критичная оптимизация.

### Зачем

Главный мотив — **TLB** (Translation Lookaside Buffer), кеш переводов «virtual → physical» в CPU. Промах TLB → page walk
через 4 уровня таблицы → 4 обращения в память на каждый перевод.

```
4 KB pages, рабочий набор 1 GB
─────────────────────────────────
страниц в наборе:   262 144
entries в TLB L1:   ~64
покрытие TLB:       64 × 4 KB  = 256 KB  ← 0.025% от рабочего набора
page walk:          4 уровня   = 4 lookups в memory на miss

2 MB huge pages, тот же 1 GB
─────────────────────────────────
страниц в наборе:   512
entries в TLB L1:   ~32 (huge TLB)
покрытие TLB:       32 × 2 MB  = 64 MB   ← 6.25% от рабочего набора
page walk:          3 уровня   = 3 lookups в memory на miss
```

Каждая huge page занимает только одну TLB entry. Меньше промахов — меньше cycles на трансляцию. Для БД, in-memory
key-value и JVM с большим heap'ом разница достигает 5–15% по throughput.

### HugeTLB — явные huge pages

«Старый» механизм. Страницы резервируются заранее, отдельно от обычной памяти, и не участвуют в swap/reclaim.

```bash
# Зарезервировать 512 страниц по 2 MB = 1 GB
echo 512 | sudo tee /proc/sys/vm/nr_hugepages

# Проверить
grep Huge /proc/meminfo
# HugePages_Total:     512
# HugePages_Free:      512
# Hugepagesize:       2048 kB

# Размер 1 GB страниц (только при поддержке CPU, pdpe1gb в /proc/cpuinfo)
ls /sys/kernel/mm/hugepages/
# hugepages-1048576kB  hugepages-2048kB
```

Использование напрямую:

```c
#include <sys/mman.h>
#include <linux/mman.h>

void *p = mmap(NULL, 2 * 1024 * 1024,
               PROT_READ|PROT_WRITE,
               MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB,
               -1, 0);
// MAP_HUGE_2MB / MAP_HUGE_1GB — выбрать конкретный размер (старшие биты flags)
```

Или через **hugetlbfs** (виртуальная ФС, монтируется в `/dev/hugepages`):

```c
int fd = open("/dev/hugepages/myfile", O_CREAT|O_RDWR, 0600);
ftruncate(fd, 2 * 1024 * 1024);
void *p = mmap(NULL, 2 * 1024 * 1024,
               PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
```

Особенности HugeTLB:

- Память резервируется заранее, при нехватке `mmap` падает с `ENOMEM`.
- Не свопится, не участвует в page cache, не fragmented.
- Используется БД (Oracle, PostgreSQL с `huge_pages=on`), DPDK, QEMU/KVM для гостевого RAM.

### THP — Transparent Huge Pages

Автоматический механизм. Ядро собирает 512 соседних 4 KB страниц в одну 2 MB huge page без участия приложения.

```
До promotion:                          После promotion:
─────────────                          ────────────────
512 × 4 KB pages                       1 × 2 MB hugepage
512 PTE entries                        1 PMD entry (huge bit set)
512 TLB-кандидатов                     1 TLB entry

┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐              ┌─────────────────────────┐
│ │ │ │ │ │ │ │ │ │ │ │ │   ...   ──▶  │     2 MB huge page      │
└─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘              └─────────────────────────┘
   4 KB каждая                              512 × 4 KB склеены
```

Управление режимом:

```bash
cat /sys/kernel/mm/transparent_hugepage/enabled
# [always] madvise never
```

| Режим     | Поведение                                 |
|-----------|-------------------------------------------|
| `always`  | THP для всех anonymous mappings           |
| `madvise` | THP только для регионов с `MADV_HUGEPAGE` |
| `never`   | THP полностью отключён                    |

`defrag` контролирует, насколько ядро готово синхронно компактить память ради huge page:

```bash
cat /sys/kernel/mm/transparent_hugepage/defrag
# always defer defer+madvise [madvise] never
```

**khugepaged** — kernel thread, который в фоне сканирует anonymous VMAs и пытается собрать соседние 4 KB страницы в huge
page (фоновая promotion). Параметры в `/sys/kernel/mm/transparent_hugepage/khugepaged/`:

```
pages_to_scan        — сколько страниц проверять за проход
scan_sleep_millisecs — пауза между проходами
alloc_sleep_millisecs— пауза после неудачной аллокации
```

Статус THP по процессу:

```bash
grep -e AnonHugePages -e ShmemHugePages /proc/$PID/status
cat /proc/$PID/smaps | grep -E '^(Size|AnonHugePages)'
```

### Подводные камни THP

- **Latency spikes**: при `always`+`defrag=always` обычный page fault может превратиться в синхронную компакцию памяти
  на сотни миллисекунд. Для latency-sensitive систем (Redis, Cassandra, MongoDB) официально рекомендуют отключать THP
  или ставить `madvise`.
- **Memory bloat**: внутренняя фрагментация — приложение, потребляющее 4 KB, удерживает 2 MB. На малых
  allocator-объектах RSS растёт быстрее ожидаемого.
- **khugepaged CPU**: при сильной фрагментации фоновый поток съедает заметный CPU, пытаясь собрать huge pages.
- **NUMA**: huge page строится из физически непрерывных страниц одного NUMA-нода; на фрагментированной системе promotion
  проваливается.
- **Не работает для file-backed маппингов** на ext4/xfs до Linux 5.x (только shmem/tmpfs). С Linux 5.x появился large
  folio / file THP, но это всё ещё ограниченное покрытие FS.

Рекомендация по умолчанию для серверов БД и in-memory систем — `madvise` + явный `MADV_HUGEPAGE` для крупных арен.

## MAP_POPULATE и prefaulting

По умолчанию `mmap` не загружает страницы сразу — они подгружаются по мере обращения (demand paging).
Флаг `MAP_POPULATE` заставляет ядро выполнить prefault при вызове `mmap`, устраняя page faults во время
работы, что важно для latency-критичных приложений:

```c
// Загрузить все страницы файла сразу, избежав page faults при обработке
char *data = mmap(NULL, file_size,
                  PROT_READ,
                  MAP_SHARED | MAP_POPULATE,
                  fd, 0);
```

## Разделяемая память между процессами

`MAP_SHARED` с анонимным маппингом (`MAP_ANONYMOUS`) работает только между процессом и его потомками
(после `fork`). Для несвязанных процессов используют POSIX shared memory:

```c
// Создатель региона
int shm_fd = shm_open("/myregion", O_CREAT | O_RDWR, 0600);
ftruncate(shm_fd, 4096);
void *shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// В другом процессе (только open, без O_CREAT):
int shm_fd2 = shm_open("/myregion", O_RDWR, 0);
void *shm2 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd2, 0);
// Теперь shm и shm2 — один и тот же физический регион памяти
```

Подробнее о межпроцессном взаимодействии — в разделе [IPC](../processes/ipc.md).

## Почему segfault не всегда при выходе за границу

Память выделяется **страницами** (обычно 4 КБ). Небольшой выход за границу массива
попадает в ту же страницу — ядро об этом не знает. `SIGSEGV` будет только при обращении
к неотображённой или защищённой странице. Именно поэтому AddressSanitizer и Valgrind необходимы
для надёжного детектирования выходов за границы буфера.

## Связанные темы

- [Виртуальная память](virtual_memory.md) — страничная организация, Page Fault, COW
- [Файловые дескрипторы](../files_and_filesystems/file_descriptors.md) — дескриптор `fd`, передаваемый в `mmap`
- [Защита памяти](memory_protection.md) — изменение прав маппинга через `mprotect`
- [Реализация malloc и free](malloc_and_free_implementation.md) — `mmap(MAP_ANONYMOUS)` для крупных блоков
- [IPC](../processes/ipc.md) — разделяемая память как механизм межпроцессного взаимодействия

## Источники

- `man 2 mmap`, `man 2 munmap`, `man 2 mremap`, `man 2 msync`, `man 2 madvise`
- `man 7 shm_overview` — POSIX разделяемая память
- [mmap — Linux man pages](https://man7.org/linux/man-pages/man2/mmap.2.html)
- [madvise(2) — Linux man pages](https://man7.org/linux/man-pages/man2/madvise.2.html)
- [HugeTLB Pages — kernel.org](https://www.kernel.org/doc/html/latest/admin-guide/mm/hugetlbpage.html)
- [Transparent Hugepage Support — kernel.org](https://www.kernel.org/doc/html/latest/admin-guide/mm/transhuge.html)
- [MADV_FREE: a new posix_madvise flag — LWN](https://lwn.net/Articles/590991/)
