# FUSE: файловая система в пользовательском пространстве

FUSE (Filesystem in Userspace) — это связка из kernel module и библиотеки libfuse, которая позволяет реализовать
полноценную файловую систему обычным userspace-процессом. Ядро не знает ничего о внутренней логике такой ФС: оно только
форвардит VFS-запросы в userspace daemon и ждёт ответа. Благодаря этому новую файловую систему можно прототипировать
за день, не прикасаясь к ядру и не рискуя уронить машину.

FUSE написана Miklos Szeredi (тем же автором, что и OverlayFS) и вошла в mainline Linux 2.6.14 в 2005 году. Сегодня
на ней построены sshfs, NTFS-3G, gocryptfs, s3fs, rclone mount и десятки других FS, для которых писать kernel-driver
было бы слишком дорого.

## Зачем нужен FUSE

Классическая разработка файловой системы для Linux требует написания kernel module: реализации `struct
super_operations`, `struct inode_operations`, `struct file_operations`, работы с VFS-кэшами (dcache, page cache),
учёта блокировок и RCU. Барьер входа высокий, цикл правка → пересборка ядра → перезагрузка измеряется минутами,
а любая ошибка приводит к kernel oops или порче данных на диске.

FUSE решает эти проблемы переносом логики в userspace:

- **Низкий барьер входа.** Минимальный hello-world на ~80 строк C; работающий прототип сетевой ФС — за день.
- **Безопасность.** Краш daemon приводит лишь к `ENOTCONN` на mountpoint. Ядро остаётся целым, ребут не нужен.
- **Любые языки и библиотеки.** Можно писать на Python, Go, Rust, использовать OpenSSL, curl, gRPC — всё, что
  недоступно в kernel space.
- **Mount без root.** Утилита `fusermount3` с битом setuid позволяет обычному пользователю смонтировать свою FS в
  свой каталог (опция `user_allow_other` в `/etc/fuse.conf`).
- **Лёгкая отладка.** Обычный gdb, strace, valgrind, AddressSanitizer работают как с любым процессом.

Цена этой свободы — производительность: каждая операция уходит в ядро и возвращается обратно в userspace, что добавляет
два-четыре context switches к каждому `read()`/`write()`. На throughput-bound нагрузках FUSE медленнее native FS
в 2–10 раз.

## Архитектура

FUSE состоит из трёх компонентов: kernel module `fuse.ko`, символьное устройство `/dev/fuse` и userspace daemon на
базе libfuse. Запрос пользователя проходит через все три.

```
                          user process (userspace)
                       ┌─────────────────────────────┐
                       │ read(fd, buf, 4096)         │
                       └──────────────┬──────────────┘
                                      │ syscall
                                      ▼
                          kernel: VFS layer
                       ┌─────────────────────────────┐
                       │ fuse_file_ops.read_iter     │
                       └──────────────┬──────────────┘
                                      │
                                      ▼
                          kernel: FUSE module
                       ┌─────────────────────────────┐
                       │ упаковка request в protocol │
                       │ кладёт в pending queue      │
                       │ блокирует процесс           │
                       └──────────────┬──────────────┘
                                      │
                                      ▼
                          /dev/fuse (chardev)
                       ┌─────────────────────────────┐
                       │ request queue (FIFO)        │
                       └──────────────┬──────────────┘
                                      │ read(/dev/fuse)
                                      ▼
                          FUSE daemon (userspace)
                       ┌─────────────────────────────┐
                       │ libfuse event loop          │
                       │ вызов callback              │
                       │ обработка (S3, SSH, crypto) │
                       │ write(/dev/fuse, reply)     │
                       └──────────────┬──────────────┘
                                      │
                                      ▼
                          kernel: FUSE module
                       ┌─────────────────────────────┐
                       │ парсит reply                │
                       │ копирует данные в user buf  │
                       │ будит заблокированный pid   │
                       └──────────────┬──────────────┘
                                      │
                                      ▼
                       возврат из read() в userspace
```

Mountpoint создаётся через `mount -t fuse` (или `fusermount3`): ядро ассоциирует superblock с открытым файловым
дескриптором `/dev/fuse`. С этого момента все VFS-операции на mountpoint конвертируются в сообщения FUSE protocol
и читаются daemon'ом через тот же fd.

## FUSE protocol

Обмен между ядром и daemon идёт бинарным протоколом с fixed-size headers и variable payload. Сообщение состоит из
`fuse_in_header` (опкод, длина, unique id запроса, nodeid цели) и опкод-специфичной нагрузки. Ответ — `fuse_out_header`
с тем же unique и опциональным payload.

Базовые опкоды:

| Опкод            | Назначение                                                                |
|------------------|---------------------------------------------------------------------------|
| `FUSE_INIT`      | handshake: согласование версии протокола и capabilities                   |
| `FUSE_LOOKUP`    | поиск имени в каталоге, возвращает nodeid и attributes                    |
| `FUSE_FORGET`    | ядро сообщает, что больше не держит ссылку на nodeid                      |
| `FUSE_GETATTR`   | чтение метаданных (size, mode, mtime, ...)                                |
| `FUSE_SETATTR`   | изменение метаданных (`chmod`, `chown`, `truncate`)                       |
| `FUSE_OPEN`      | открытие файла, daemon возвращает `fh` (file handle)                      |
| `FUSE_READ`      | чтение данных по `fh` и offset                                            |
| `FUSE_WRITE`     | запись данных по `fh` и offset                                            |
| `FUSE_RELEASE`   | закрытие открытого ранее `fh`                                             |
| `FUSE_READDIR`   | перечисление содержимого каталога                                         |
| `FUSE_INTERRUPT` | отмена висящего запроса (например, при Ctrl-C)                            |

Каждый файловый объект идентифицируется парой (`nodeid`, `generation`). `nodeid` — 64-битный идентификатор, который
выбирает daemon при ответе на `FUSE_LOOKUP`; ядро затем использует его во всех последующих запросах. `generation`
позволяет переиспользовать nodeid: если запись удалили и создали заново, ядро увидит другое значение и не перепутает
объекты.

`FUSE_FORGET` — единственный «бескровный» опкод: ядро не ждёт ответа. Он нужен потому, что daemon может держать
тяжёлый объект (открытое соединение, дескриптор удалённого файла) и хочет освободить его, когда ядро больше не
держит ссылку.

## libfuse API

В пакете `libfuse3` есть два уровня API: high-level и low-level. Они различаются единицей идентификации объекта.

### High-level: callbacks по путям

В `fuse.h` callbacks принимают строковые пути. Библиотека сама поддерживает таблицу nodeid и переводит lookup'ы в
пути; daemon работает в привычных терминах.

```c
#include <fuse.h>

static int my_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }
    if (strcmp(path, "/hello.txt") == 0) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = 13;
        return 0;
    }
    return -ENOENT;
}

static int my_read(const char *path, char *buf, size_t size,
                   off_t off, struct fuse_file_info *fi) {
    const char *data = "Hello, FUSE\n";
    size_t len = strlen(data);
    if (off >= (off_t)len) return 0;
    if (off + size > len) size = len - off;
    memcpy(buf, data + off, size);
    return size;
}

static const struct fuse_operations ops = {
    .getattr = my_getattr,
    .readdir = my_readdir,
    .read    = my_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &ops, NULL);
}
```

Собрать и смонтировать:

```bash
cc -Wall hello.c $(pkg-config --cflags --libs fuse3) -o hellofs
mkdir /tmp/fusemnt
./hellofs /tmp/fusemnt
cat /tmp/fusemnt/hello.txt        # → Hello, FUSE
fusermount3 -u /tmp/fusemnt
```

High-level API удобен для прототипов и read-mostly FS, но имеет цену: на каждый запрос libfuse строит полный путь и
перевычисляет lookup. Для большой иерархии это заметно.

### Low-level: callbacks по nodeid

`fuse_lowlevel.h` отдаёт сырой протокол: каждый callback получает `fuse_req_t` и `fuse_ino_t`, а ответ формируется
функциями `fuse_reply_*`. Daemon сам управляет таблицей nodeid и контролирует, когда ядро может «забывать» inode.

```c
static void ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct fuse_entry_param e = { /* fill nodeid, attr, generation, timeouts */ };
    fuse_reply_entry(req, &e);
}

static void ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                    off_t off, struct fuse_file_info *fi) {
    fuse_reply_buf(req, data + off, size);
}
```

Low-level быстрее (нет перебора пути и повторных lookup'ов), но требует ручной работы с временем жизни nodeid и
поэтому используется в production-системах: sshfs, gluster fuse client, libfuse-bench.

## Жизнь одного read()

```
 t0:  user calls read(fd, buf, 4096)              ── userspace
 t1:  syscall enter → VFS → fuse_file_operations  ── kernel
 t2:  FUSE module формирует fuse_in_header
      + struct fuse_read_in (fh, offset, size)
 t3:  кладёт запрос в pending queue
 t4:  процесс уходит в TASK_INTERRUPTIBLE sleep
 ─── переключение контекста ───
 t5:  daemon в fuse_session_loop делает read(/dev/fuse)
      ─ ядро отдаёт ему готовый запрос
 t6:  libfuse вызывает соответствующий callback
 t7:  callback делает свою работу (запрос в S3,
      чтение из локального файла, расшифровка, ...)
 t8:  callback возвращает данные через fuse_reply_buf
 t9:  libfuse делает write(/dev/fuse, reply)
 ─── переключение контекста ───
 t10: FUSE module копирует данные в user buffer
 t11: будит процесс, возвращает size из read()
 t12: user видит данные                            ── userspace
```

За один пользовательский `read()` происходит как минимум четыре пересечения границы user/kernel и два context switch
между процессом-клиентом и daemon. Это и есть базовая стоимость FUSE.

## Минимальный пример: hello-world

Файл `hellofs.c` — полностью самодостаточная FUSE FS, монтирующая виртуальный каталог с одним файлом `hello.txt`:

```c
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <string.h>
#include <errno.h>

static const char *content = "Hello, FUSE\n";

static int hello_getattr(const char *path, struct stat *st,
                         struct fuse_file_info *fi) {
    memset(st, 0, sizeof(*st));
    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    } else if (strcmp(path, "/hello.txt") == 0) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size = strlen(content);
    } else return -ENOENT;
    return 0;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t off, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags) {
    if (strcmp(path, "/") != 0) return -ENOENT;
    filler(buf, ".",         NULL, 0, 0);
    filler(buf, "..",        NULL, 0, 0);
    filler(buf, "hello.txt", NULL, 0, 0);
    return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/hello.txt") != 0) return -ENOENT;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
    return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t off,
                      struct fuse_file_info *fi) {
    size_t len = strlen(content);
    if (off >= (off_t)len) return 0;
    if (off + size > len) size = len - off;
    memcpy(buf, content + off, size);
    return size;
}

static const struct fuse_operations ops = {
    .getattr = hello_getattr,
    .readdir = hello_readdir,
    .open    = hello_open,
    .read    = hello_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &ops, NULL);
}
```

```bash
cc hellofs.c $(pkg-config --cflags --libs fuse3) -o hellofs
mkdir -p /tmp/fusemnt
./hellofs -f /tmp/fusemnt &        # -f = foreground, удобно для отладки
ls /tmp/fusemnt                    # → hello.txt
cat /tmp/fusemnt/hello.txt         # → Hello, FUSE
fusermount3 -u /tmp/fusemnt
```

## Производительность

Каждая FS-операция стоит как минимум двух context switches и нескольких copy между пространствами. На бенчмарках
типа fio FUSE-обёртка вокруг tmpfs показывает 2–10× деградации throughput и заметное увеличение latency. libfuse и
ядро дают несколько рычагов, чтобы это смягчить.

### Кэширование

| Опция                 | Что делает                                                                |
|-----------------------|---------------------------------------------------------------------------|
| `-o kernel_cache`     | ядро кэширует данные read'ов; FUSE_READ не зовётся повторно               |
| `-o auto_cache`       | то же, но кэш сбрасывается при изменении mtime/size                       |
| `-o writeback_cache`  | write'ы буферизуются в page cache и сливаются батчами                     |
| `-o entry_timeout=N`  | сколько секунд ядро кэширует результат FUSE_LOOKUP                        |
| `-o attr_timeout=N`   | сколько секунд кэшируется результат FUSE_GETATTR                          |
| `-o negative_timeout` | кэш отрицательных lookup'ов (имя точно не существует)                     |

Кэширование — палка о двух концах: для read-mostly данных оно даёт многократный выигрыш, для часто меняющихся —
показывает stale-данные. В sshfs `-o cache=yes` поднимает throughput директорий на порядок, но создаёт окно
неконсистентности относительно удалённого файла.

### Batch и многопоточность

- **FUSE_CAP_BIG_WRITES** — ядро отправляет write блоками до 1 MiB вместо 4 KiB. Без этой capability writes идут
  страница за страницей и убивают throughput.
- **`fuse_session_loop_mt`** — multi-threaded event loop в libfuse: запросы разбираются пулом воркеров параллельно.
  По умолчанию используется и обычно даёт лучший результат, чем однопоточный `fuse_session_loop`.
- **FUSE_CAP_SPLICE_READ / SPLICE_WRITE** — передача данных через `splice(2)` без копирования в userspace буфер;
  актуально для FS-прокси (sshfs, NFS-overFUSE).
- **FUSE_CAP_ASYNC_READ** — ядро может слать несколько read-запросов параллельно, не дожидаясь ответа.

### virtio-fs как обход FUSE

Для виртуальных машин был придуман **virtio-fs**: shared filesystem гость↔хост, использующий FUSE protocol поверх
virtio transport, минуя `/dev/fuse`. В госте установлен kernel module `virtiofs`, на хосте — `virtiofsd`. Это
ускоряет shared-FS в KVM в 2–3 раза по сравнению с virtio-9p и используется как стандартное решение в Kata
Containers и Firecracker (см. [virtio](../virtualization/virtio.md)).

## Реальные FUSE-файловые системы

| Проект                  | Назначение                                                          |
|-------------------------|---------------------------------------------------------------------|
| **sshfs**               | mount remote SSH-сервера как локальной FS                           |
| **gocryptfs**           | encrypted overlay поверх обычного каталога                          |
| **encfs**               | устаревший аналог gocryptfs, всё ещё встречается                    |
| **s3fs-fuse**           | S3 bucket как локальная FS                                          |
| **goofys**              | S3, оптимизированный под throughput (sequential I/O)                |
| **rclone mount**        | универсальный mount для десятков cloud-провайдеров                  |
| **NTFS-3G**             | полноценный read/write доступ к NTFS                                |
| **bindfs**              | re-mount каталога с другим владельцем/правами                       |
| **mergerfs**            | union mount нескольких дисков без RAID                              |
| **unionfs-fuse**        | альтернатива OverlayFS в userspace                                  |
| **gluster fuse client** | клиент distributed FS Gluster                                       |
| **CephFS fuse client**  | альтернатива kernel-клиенту Ceph                                    |

Многие из них реализованы на low-level API ради производительности (sshfs, gluster) или используют bindings к
другим языкам — Go (rclone, gocryptfs), Rust (rust-fuse), Python (pyfuse3).

## Пример: путь запроса в sshfs

sshfs — типичный proxy-FS: каждая FUSE-операция превращается в SFTP-запрос по уже открытому SSH-соединению.

```
 cat /mnt/remote/log.txt
        │
        ▼
 ┌─────────────────────┐
 │ VFS → fuse_kernel   │
 │ FUSE_OPEN  /log.txt │
 └──────────┬──────────┘
            │
            ▼
 ┌─────────────────────┐
 │ sshfs daemon        │
 │  ─ берёт SFTP канал │
 │  ─ шлёт SSH_FXP_OPEN│
 └──────────┬──────────┘
            │ TCP
            ▼
 ┌─────────────────────┐
 │ remote sshd + sftp  │
 │  open(/var/log/...) │
 └──────────┬──────────┘
            │ TCP (handle)
            ▼
 ┌─────────────────────┐
 │ sshfs reply         │
 │  fuse_reply_open(fh)│
 └──────────┬──────────┘
            │
 ── ядро возвращает fd пользователю ──
            │
            ▼
       read(fd, ...)        ── повторяется тот же круг через
            │                  FUSE_READ → SSH_FXP_READ
            ▼
        ... данные ...
```

Latency определяется RTT до удалённого хоста, помноженным на число round-trip'ов. Поэтому в sshfs так важны
`-o Compression=yes`, кэширование и `-o ServerAliveInterval` — каждый round-trip оплачивается дважды (FUSE + TCP).

## Ограничения

- **Производительность.** Каждый запрос — context switch; на random I/O разница с native FS особенно заметна.
- **`O_DIRECT` не поддерживается.** Нет смысла «обходить» page cache, если данные всё равно копируются между ядром и
  daemon.
- **Сложно эмулировать POSIX-семантику.** Полноценный `flock`/`fcntl`-locking требует FUSE_GETLK/FUSE_SETLK callbacks
  и сериализации в daemon. ACL и xattr требуют отдельной поддержки.
- **mmap ограничен.** Shared writeable mmap работает только при `-o writeback_cache`. Без неё — лишь read-only mmap.
- **Stat-storm.** Утилиты типа `ls -l` на большом каталоге дают шквал GETATTR-запросов; кэширование решает, но
  ценой свежести данных.
- **Привилегии.** `mount` через `fusermount3` доступен пользователю, но `allow_other` и mount внутри chroot
  требуют дополнительной настройки `/etc/fuse.conf`.

## Альтернативы

- **virtio-fs** — для shared FS между VM и хостом, отдельный transport, без `/dev/fuse`.
- **NFS / NFSv4** — для distributed access по сети; ядерный клиент, лучше кэширование.
- **9P / 9pfs** — простой протокол из Plan 9, используется в QEMU virtio-9p и WSL2 для интеграции с Windows.
- **CUSE (Character device in Userspace)** — близкий родственник FUSE для эмуляции `/dev/*` устройств в userspace.
- **Собственный kernel module** — когда FUSE-overhead неприемлем (production storage, low-latency).

## Отладка

```bash
# Выполнение в foreground, лог в stderr
./myfs -f /mnt/myfs

# Полный трейс FUSE-запросов и ответов
./myfs -d /mnt/myfs

# strace на FUSE-daemon показывает все read/write по /dev/fuse
strace -f -e read,write,ioctl ./myfs /mnt/myfs

# Принудительный umount, если daemon завис
fusermount3 -uz /mnt/myfs        # -z = lazy unmount
```

При зависшем daemon ядро возвращает `ENOTCONN` на любую операцию через mountpoint; `umount -l` или `fusermount3 -uz`
освобождает дерево, фактический cleanup произойдёт, когда исчезнет последний открытый файл.

## Связанные темы

- [Основы файловых систем](filesystems_basics.md) — VFS-слой, через который FUSE проксирует запросы
- [OverlayFS и слои Docker](overlayfs.md) — union mount в ядре, написан тем же автором, что и FUSE
- [Блочные и символьные устройства](block_and_character_devices.md) — `/dev/fuse` как точка обмена с ядром
- [virtio](../virtualization/virtio.md) — virtio-fs обходит `/dev/fuse` для shared FS в KVM
- [Файловые дескрипторы](file_descriptors.md) — открытый `/dev/fuse` — это обычный fd

## Источники

- [libfuse on GitHub](https://github.com/libfuse/libfuse) — исходники libfuse3 и примеры
- [FUSE — kernel.org documentation](https://www.kernel.org/doc/html/latest/filesystems/fuse.html)
- [To FUSE or not to FUSE — USENIX FAST'17](https://www.usenix.org/conference/fast17/technical-sessions/presentation/vangoor)
- [virtio-fs design](https://virtio-fs.gitlab.io/)
- `man 4 fuse`, `man 8 mount.fuse3`, `man 1 fusermount3`
