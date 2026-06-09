# inotify и fanotify: отслеживание событий файловой системы

В обычной модели программа узнаёт об изменении файла, только когда сама его прочитает. Этого достаточно, пока файлы
меняет одна программа, но картина ломается, как только в игре участвуют несколько процессов или внешние действия:
редактор должен заметить, что файл переписала git checkout; антивирус — что в `/tmp` появился новый исполняемый
файл; backup-демон — что директория изменилась за последние десять минут.

Старый ответ — periodic `stat()` (polling), но он плох: масштабируется как O(N) по числу файлов на каждый интервал,
не видит быстрых rename → modify → rename и пропускает события между опросами. Linux отвечает на это двумя
specialized kernel-механизмами: **inotify** (общего назначения) и **fanotify** (повышенные привилегии, mount-scope,
permission events).

## Зачем это нужно

- **IDE и редакторы** — auto-reload файлов, изменённых снаружи (VSCode, IntelliJ, vim watcher).
- **Антивирусы** — сканировать каждый открываемый или новый исполняемый файл (ClamAV, Sophos, McAfee).
- **Incremental backup** — резервное копирование только изменённого с прошлой сессии.
- **Feature flags / config reload** — перезагрузить конфиг сразу после `vim app.yaml`, а не раз в минуту.
- **Container runtimes** — отслеживать изменения в bind-mount каталогах хоста.
- **Hot reload** — webpack-dev-server, nodemon, cargo-watch, gunicorn `--reload`.
- **Indexers** — Spotlight-подобные системы (recoll, baloo) перестраивают индекс только по changed-файлам.

Все эти задачи объединяет одно: реакция на событие должна происходить за миллисекунды, а сам watcher не должен
систематически дёргать ФС.

## Эволюция API

| Поколение    | Версия ядра     | Состояние                                      |
|--------------|-----------------|------------------------------------------------|
| **dnotify**  | Linux 2.4, 2001 | устарело: сигналы вместо fd, per-dir, неудобно |
| **inotify**  | Linux 2.6.13    | стандарт для редакторов и IDE                  |
| **fanotify** | Linux 2.6.36    | mount-wide, permission events, AV-классы       |

dnotify передавал события через сигналы (`SIGIO`): подписчик получал лишь номер сигнала и должен был сам делать
`stat()` всех файлов в каталоге, чтобы понять, что именно изменилось. Один сигнал на каталог — ни о каком mass
watching речи не шло. Замена пришла с inotify, который ввёл file-descriptor-based API и структурированные события.

## inotify

### API

inotify оперирует тремя сущностями:

- **inotify instance** — открытый fd, через который читаются события.
- **watch descriptor (wd)** — int, идентификатор подписки на конкретный путь.
- **event mask** — битовая маска интересующих событий.

```c
int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
int wd = inotify_add_watch(fd, "/tmp/watched", IN_CREATE | IN_MODIFY | IN_DELETE);
// ... epoll/poll/read на fd ...
inotify_rm_watch(fd, wd);
close(fd);
```

При наступлении события ядро формирует запись `struct inotify_event` и кладёт в очередь fd:

```c
struct inotify_event {
    int      wd;       /* watch descriptor */
    uint32_t mask;     /* event mask */
    uint32_t cookie;   /* для связи IN_MOVED_FROM ↔ IN_MOVED_TO */
    uint32_t len;      /* длина name[] (с \0 и padding) */
    char     name[];   /* имя файла (если событие на содержимом каталога) */
};
```

`cookie` — ненулевой идентификатор, через который связываются парные события переименования (`IN_MOVED_FROM`
+ `IN_MOVED_TO`); если значения cookie совпадают — это один и тот же rename.

### Базовые события

| Маска              | Когда генерируется                                                    |
|--------------------|-----------------------------------------------------------------------|
| `IN_ACCESS`        | файл был прочитан                                                     |
| `IN_MODIFY`        | в файл записали                                                       |
| `IN_ATTRIB`        | изменились метаданные (chmod, chown, timestamps, link count)          |
| `IN_OPEN`          | файл открыт                                                           |
| `IN_CLOSE_WRITE`   | закрыт открытый для записи fd                                         |
| `IN_CLOSE_NOWRITE` | закрыт открытый только на чтение fd                                   |
| `IN_CREATE`        | в каталоге создан новый объект                                        |
| `IN_DELETE`        | объект в каталоге удалён                                              |
| `IN_DELETE_SELF`   | удалён сам отслеживаемый объект                                       |
| `IN_MOVED_FROM`    | объект переименован: «откуда»                                         |
| `IN_MOVED_TO`      | объект переименован: «куда»                                           |
| `IN_MOVE_SELF`     | сам watched-объект был переименован                                   |
| `IN_Q_OVERFLOW`    | очередь событий переполнилась — события потеряны                      |
| `IN_IGNORED`       | watch стал недействительным (объект удалён или fs размонтирована)     |

Дополнительные модификаторы: `IN_ONLYDIR` (отказать, если путь не каталог), `IN_DONT_FOLLOW` (не разрешать symlink),
`IN_EXCL_UNLINK` (не доставлять события для unlink'нутых, но открытых файлов), `IN_MASK_ADD` (объединить с
существующей маской), `IN_ONESHOT` (одноразовый watch).

### Поток данных

```
 process A                  kernel                          process B (watcher)
 ┌────────────┐
 │ write(fd…) │
 └─────┬──────┘
       │ syscall
       ▼
 ┌─────────────────────────────────────────┐
 │              VFS                        │
 │  notify_change / fsnotify_modify        │
 └───────────────────┬─────────────────────┘
                     │
                     ▼
 ┌─────────────────────────────────────────┐
 │       fsnotify backend (inotify)        │
 │   ─ ищет watchers на этом inode/dir     │
 │   ─ формирует struct inotify_event      │
 │   ─ кладёт в per-instance event queue   │
 └───────────────────┬─────────────────────┘
                     │
                     ▼
 ┌─────────────────────────────────────────┐
 │   event queue                           │
 │   [ev1][ev2][ev3] ...                   │  ◀── read(inotify_fd) ──┐
 │   max_queued_events (~16384 default)    │                         │
 └─────────────────────────────────────────┘                         │
                                                                     │
                                                              ┌──────┴──────┐
                                                              │ epoll_wait  │
                                                              │ → read()    │
                                                              │ → разобрать │
                                                              │   struct    │
                                                              └─────────────┘
```

### Полный пример

Простейший watcher, печатающий события на `/tmp/watched`:

```c
#include <sys/inotify.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BUF_LEN (1024 * (sizeof(struct inotify_event) + NAME_MAX + 1))

static const struct { uint32_t mask; const char *name; } EVENTS[] = {
    {IN_CREATE,        "CREATE"},
    {IN_DELETE,        "DELETE"},
    {IN_MODIFY,        "MODIFY"},
    {IN_MOVED_FROM,    "MOVED_FROM"},
    {IN_MOVED_TO,      "MOVED_TO"},
    {IN_ATTRIB,        "ATTRIB"},
    {IN_OPEN,          "OPEN"},
    {IN_CLOSE_WRITE,   "CLOSE_WRITE"},
    {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
    {IN_Q_OVERFLOW,    "Q_OVERFLOW"},
};

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <path>\n", argv[0]); return 1; }

    int fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0) { perror("inotify_init1"); return 1; }

    int wd = inotify_add_watch(fd, argv[1],
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO |
        IN_ATTRIB | IN_OPEN | IN_CLOSE_WRITE);
    if (wd < 0) { perror("inotify_add_watch"); return 1; }

    char buf[BUF_LEN] __attribute__((aligned(8)));
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) { if (errno == EINTR) continue; perror("read"); break; }

        for (char *p = buf; p < buf + n; ) {
            struct inotify_event *ev = (struct inotify_event *)p;
            printf("wd=%d cookie=%u ", ev->wd, ev->cookie);
            for (size_t i = 0; i < sizeof(EVENTS)/sizeof(*EVENTS); ++i)
                if (ev->mask & EVENTS[i].mask) printf("%s ", EVENTS[i].name);
            if (ev->len > 0) printf("name=%s", ev->name);
            putchar('\n');
            p += sizeof(*ev) + ev->len;
        }
    }
    return 0;
}
```

```bash
cc watch.c -o watch
./watch /tmp/watched &
touch /tmp/watched/foo            # → CREATE OPEN ATTRIB CLOSE_WRITE name=foo
echo hi > /tmp/watched/foo        # → OPEN MODIFY CLOSE_WRITE name=foo
mv /tmp/watched/foo /tmp/watched/bar  # → MOVED_FROM ... MOVED_TO (одинаковый cookie)
rm /tmp/watched/bar               # → DELETE name=bar
```

### Ограничения inotify

- **Не recursive.** Watch ставится на конкретный каталог. Чтобы следить за деревом, программа сама делает `nftw()`
  и `inotify_add_watch` на каждую subdir — и потом обрабатывать `IN_CREATE` для нового каталога, добавляя watch
  на него (с гонкой между walk и появлением файлов).
- **`max_user_watches`.** `/proc/sys/fs/inotify/max_user_watches` ограничивает число watch descriptors на user uid.
  Дефолт — 8192–65536 в зависимости от дистрибутива; для IDE на больших monorepos этого не хватает, и тогда
  возвращается `ENOSPC`. Стандартный фикс: `sysctl fs.inotify.max_user_watches=524288`.
- **`max_queued_events`.** При переполнении очереди ядро доставляет одно событие `IN_Q_OVERFLOW` и теряет всё, что
  не успел прочитать watcher. После переполнения корректная стратегия — пересканировать дерево.
- **Symlinks.** По умолчанию следит за target. `IN_DONT_FOLLOW` принуждает работать с самим symlink.
- **Не работает на сетевых ФС.** Изменения, сделанные на удалённой стороне NFS/SMB, не приходят локальному
  inotify — события генерируются только на хосте, где произошёл VFS-вызов.
- **bind mounts.** Watch ставится по inode, поэтому события приходят независимо от того, через какой mountpoint
  открыт файл — это иногда удивляет.

### Утилиты

```bash
inotifywait -m -r /etc           # рекурсивный CLI watcher
inotifywatch -t 60 /var/log      # статистика событий
```

## fanotify

### Что добавляет fanotify

inotify подписывается на путь (inode); fanotify — на **mountpoint** или **filesystem**, и опционально может
**блокировать** операцию до решения watcher'а. Это делает его пригодным для:

- on-access антивирусных сканеров (FAN_OPEN_EXEC_PERM);
- HSM (hierarchical storage manager): перехватить чтение и «дотянуть» данные с tape;
- audit-систем уровня файловой системы.

### API

```c
int fan = fanotify_init(FAN_CLOEXEC | FAN_NONBLOCK | FAN_CLASS_CONTENT,
                        O_RDONLY | O_LARGEFILE);

fanotify_mark(fan,
              FAN_MARK_ADD | FAN_MARK_MOUNT,   /* per-mount watch */
              FAN_OPEN | FAN_CLOSE_WRITE,
              AT_FDCWD, "/");

// ... epoll/read ...
```

В отличие от inotify, события приходят как `struct fanotify_event_metadata`, в котором ядро уже **открыло fd**
интересующего файла:

```c
struct fanotify_event_metadata {
    __u32 event_len;
    __u8  vers;
    __u8  reserved;
    __u16 metadata_len;
    __aligned_u64 mask;
    __s32 fd;        /* открытый ядром fd объекта события */
    __s32 pid;       /* pid, сделавший операцию */
};
```

Получая fd напрямую, watcher избегает классической гонки «имя → файл»: к моменту, когда antivirus откроет файл по
имени, имя могло уже указывать на другой объект. С fd этой проблемы нет.

### Классы и режимы

| Класс                   | Назначение                                                |
|-------------------------|-----------------------------------------------------------|
| `FAN_CLASS_NOTIF`       | только уведомления (как inotify, но per-mount)            |
| `FAN_CLASS_CONTENT`     | permission на content access (AV, audit)                  |
| `FAN_CLASS_PRE_CONTENT` | permission до содержания (HSM: подтянуть данные с tape)   |

При смешанной подписке события доставляются в порядке: PRE_CONTENT → CONTENT → NOTIF. Это даёт правильный порядок
для AV-стека: сначала HSM подтянет данные, потом AV их просканирует.

### Permission events

```
 process X                kernel              fanotify daemon (AV)
 ┌────────────┐
 │ open(F, R) │
 └─────┬──────┘
       │ syscall
       ▼
 ┌─────────────────────────────────────┐
 │  VFS                                │
 │  fsnotify_perm(FAN_OPEN_PERM)       │
 └──────────────┬──────────────────────┘
                │
                ▼
 ┌─────────────────────────────────────┐
 │  fanotify queue                     │
 │  блокирует процесс X                │
 │  событие с fd и pid                 │ ─── read(fan_fd) ────▶ ┌────────────┐
 └─────────────────────────────────────┘                        │ AV scans   │
                                                                │ file via fd│
                                                                └──────┬─────┘
                                                                       │
                                          ┌─── write({fd, FAN_ALLOW}) ─┘
                                          │
                                          ▼
 ┌─────────────────────────────────────┐
 │  fanotify decision                  │
 │  → ALLOW: open() возвращает ok      │
 │  → DENY:  open() возвращает -EPERM  │
 └──────────────┬──────────────────────┘
                │
                ▼
        возврат из open()
```

`fanotify_response.response = FAN_ALLOW | FAN_DENY` принимается ядром; если daemon вышел молча, ядро возвращает
дефолтное значение по истечении таймаута. Bad-poll-loop в AV напрямую транслируется в зависший `open()`
пользовательского процесса — отсюда требование высокой надёжности и низкого latency.

### Привилегии и capabilities

| Операция                                     | Нужна capability       |
|----------------------------------------------|------------------------|
| `FAN_CLASS_NOTIF` per-inode (since Linux 5.1)| `CAP_SYS_ADMIN` опц.   |
| `FAN_MARK_MOUNT`                             | `CAP_SYS_ADMIN`        |
| `FAN_MARK_FILESYSTEM` (since Linux 4.20)     | `CAP_SYS_ADMIN`        |
| permission events                            | `CAP_SYS_ADMIN`        |
| `FAN_REPORT_FID` (since Linux 5.1)           | без CAP                |

С Linux 5.1 появился `FAN_REPORT_FID`: событие содержит не fd, а filesystem id + handle, что позволяет работать
без `CAP_SYS_ADMIN` и без открытия файла на каждое событие — экономия дескрипторов на массовых нагрузках.

### Recursive по mount

`FAN_MARK_MOUNT` подписывает на все файлы, доступные через данный mountpoint:

```c
fanotify_mark(fan, FAN_MARK_ADD | FAN_MARK_MOUNT,
              FAN_OPEN, AT_FDCWD, "/home");
```

Это даёт рекурсивный watch одним системным вызовом — главное преимущество перед inotify, который требует обхода
дерева и добавления watch на каждую directory.

### Минимальный fanotify-монитор

Простейший трассер всех `open()` на корневой ФС (требует `sudo`):

```c
#define _GNU_SOURCE
#include <sys/fanotify.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

int main(void) {
    int fan = fanotify_init(FAN_CLOEXEC | FAN_CLASS_NOTIF,
                            O_RDONLY | O_LARGEFILE);
    if (fan < 0) { perror("fanotify_init"); return 1; }

    if (fanotify_mark(fan, FAN_MARK_ADD | FAN_MARK_MOUNT,
                      FAN_OPEN | FAN_CLOSE_WRITE, AT_FDCWD, "/") < 0) {
        perror("fanotify_mark"); return 1;
    }

    char buf[4096];
    for (;;) {
        ssize_t n = read(fan, buf, sizeof(buf));
        if (n <= 0) { perror("read"); break; }

        struct fanotify_event_metadata *m = (void *)buf;
        for (; FAN_EVENT_OK(m, n); m = FAN_EVENT_NEXT(m, n)) {
            char path[PATH_MAX] = "?";
            if (m->fd >= 0) {
                char proc_link[64];
                snprintf(proc_link, sizeof(proc_link),
                         "/proc/self/fd/%d", m->fd);
                ssize_t r = readlink(proc_link, path, sizeof(path) - 1);
                if (r > 0) path[r] = '\0';
                close(m->fd);
            }
            printf("pid=%d mask=0x%llx %s\n",
                   m->pid, (unsigned long long)m->mask, path);
        }
    }
    return 0;
}
```

`readlink("/proc/self/fd/N")` — стандартный способ восстановить путь из открытого fd, потому что fanotify
передаёт именно дескриптор. Каждый `m->fd` обязательно нужно закрыть, иначе быстро упрётесь в `RLIMIT_NOFILE`.

## inotify vs fanotify

| Свойство             | inotify                          | fanotify                                  |
|----------------------|----------------------------------|-------------------------------------------|
| Granularity          | per-watch (inode/path)           | per-mount, per-FS, per-inode (5.1+)       |
| Recursive            | вручную (walk + per-dir watch)   | `FAN_MARK_MOUNT` / `FAN_MARK_FILESYSTEM`  |
| Возвращает FD        | нет, только имя                  | да, ядро уже открыло fd                   |
| Permission events    | нет                              | да (FAN_*_PERM с 2.6.36, расширения 5.x)  |
| pid инициатора       | нет                              | да                                        |
| Привилегии           | unprivileged                     | часто требует `CAP_SYS_ADMIN`             |
| Лимиты               | `max_user_watches`               | `fanotify_groups`, `fanotify_marks`       |
| Сетевые ФС           | не работает                      | работает (mount-level, NFS/SMB)           |
| Поддержка ядра       | Linux 2.6.13+                    | Linux 2.6.36+, активно развивается        |
| Главное применение   | редакторы, IDE, hot reload       | антивирусы, HSM, audit                    |

Архитектурно оба механизма строятся поверх одного fsnotify backend в ядре, но представляют разные политики и
разные граничные условия использования.

```
                    ┌──────────────────────────┐
                    │  fsnotify backend (ядро) │
                    │  hooks в VFS:            │
                    │   ─ fsnotify_modify      │
                    │   ─ fsnotify_open        │
                    │   ─ fsnotify_close       │
                    │   ─ fsnotify_perm        │
                    └─────┬────────────┬───────┘
                          │            │
              ┌───────────┘            └────────────┐
              ▼                                     ▼
       ┌─────────────┐                       ┌─────────────┐
       │  inotify    │                       │  fanotify   │
       │  group      │                       │  group      │
       │  per-inode  │                       │  per-mount /│
       │  per-process│                       │  per-FS     │
       └─────────────┘                       └─────────────┘
              │                                     │
              ▼                                     ▼
       struct inotify_event             struct fanotify_event_metadata
       (имя + маска)                    (fd + маска + pid)
              │                                     │
              ▼                                     ▼
        watcher process                     watcher с CAP_SYS_ADMIN
        (IDE, hot reload)                   (AV, HSM, audit)
```

## Реальные продукты

- **VSCode, IntelliJ, JetBrains-семейство** — inotify. На больших monorepo упираются в `max_user_watches` и
  показывают баннер «file watcher limit reached».
- **systemd path units** — `PathChanged=`, `PathModified=`, `PathExistsGlob=`. Реализованы поверх inotify внутри
  systemd.
- **incrond** — cron-like демон, запускающий команду по inotify-событиям. `/etc/incron.d/*` в формате
  `<path> <mask> <command>`.
- **ClamAV on-access scanner** — fanotify permission events, FAN_OPEN_PERM, блокирует `open()` до завершения скана.
- **Sophos / McAfee Endpoint** — тот же подход: fanotify в FAN_CLASS_CONTENT.
- **HSM в Lustre, IBM Spectrum Scale** — fanotify FAN_CLASS_PRE_CONTENT для прозрачной подкачки данных.
- **inotify-tools** — `inotifywait` и `inotifywatch` для CLI-сценариев и тестирования.
- **chokidar / watchdog / fsnotify (Go)** — кроссплатформенные библиотеки watcher'ов, на Linux используют inotify.
- **systemd-journald** — следит за `/var/log/journal` через inotify, чтобы подхватить новые binary log файлы.

## Производительность

- **Память на watch.** Каждый inotify watch стоит ~1 KB в ядре. 100 000 watches на пользователя — порядка 100 MB
  kernel memory. fanotify mount-level подписки несравнимо дешевле.
- **Throughput.** При тысячах событий в секунду эффективнее читать `read()` большим буфером и разбирать его
  в цикле (как в примере выше), а не вызывать `read()` на каждое событие.
- **Q_OVERFLOW recovery.** При получении `IN_Q_OVERFLOW` единственный правильный путь — пересканировать всё
  дерево (или mount). Игнорировать переполнение — потерять консистентность.
- **Batch обработка.** Координировать события: rename выглядит как пара MOVED_FROM/MOVED_TO с одинаковым cookie;
  обработка по одному ведёт к ложным удалениям.
- **Дебоунс.** Сохранение файла редактором часто генерирует серию: CREATE tmp → MODIFY → CLOSE_WRITE tmp →
  MOVED_FROM tmp → MOVED_TO file. Если реагировать на каждый — будет лишняя работа. Стандартный приём:
  собирать события за окно 50–200 мс и применять последнее состояние.

## Когда что выбирать

- **inotify**: редакторы, hot reload, CLI-утилиты, обычные пользовательские задачи без необходимости блокировать
  операции.
- **fanotify**: enterprise security (AV, audit), HSM, mount-wide мониторинг, когда recursive walk слишком дорог
  и есть рутовые права.
- **polling через `stat()`**: только если ничего из вышеперечисленного недоступно (например, удалённый NFS-volume,
  Windows-share без notification API). Используйте увеличенный интервал и отдельный поток.

## Связанные темы

- [Файловые дескрипторы](file_descriptors.md) — inotify/fanotify instance возвращается как обычный fd
- [Основы файловых систем](filesystems_basics.md) — VFS-уровень, где сидят fsnotify-хуки
- [Открытые файлы и процессы](open_files_and_processes.md) — `lsof`, `/proc/<pid>/fd` для отладки watcher'ов
- [Операции с директориями](directory_operations.md) — `nftw`/`opendir` для recursive walk перед подпиской inotify
- [FUSE](fuse.md) — пример FS, где inotify работает только на стороне daemon, не клиентов

## Источники

- [inotify(7) — man7.org](https://man7.org/linux/man-pages/man7/inotify.7.html)
- [fanotify(7) — man7.org](https://man7.org/linux/man-pages/man7/fanotify.7.html)
- [fsnotify — kernel.org documentation](https://www.kernel.org/doc/html/latest/admin-guide/filesystem-monitor.html)
- [LWN: fanotify, three years on](https://lwn.net/Articles/716321/)
- [inotify-tools on GitHub](https://github.com/inotify-tools/inotify-tools)
- `man 2 inotify_init`, `man 2 inotify_add_watch`, `man 2 fanotify_init`, `man 2 fanotify_mark`
