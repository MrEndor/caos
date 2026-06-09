# Файлы и файловые системы

В Linux всё является файлом — жёсткий принцип проектирования, пронизывающий всю систему. Обычные файлы, каталоги,
устройства, каналы, сокеты и даже информация о процессах доступна через единый интерфейс: `open`, `read`, `write`,
`close`. Этот раздел объясняет, как этот интерфейс устроен изнутри — от системных вызовов до структур ядра.

Вы узнаете, что такое файловый дескриптор и таблица открытых файлов, как ядро организует данные на диске через inode и
суперблок, как работают жёсткие и символические ссылки, чем блочные устройства отличаются от символьных и как права
доступа применяются на уровне ядра. Отдельное внимание уделено практике: реализация `cp`, `mv`, `rm`, `ls` и `tee` через
системные вызовы.

## Темы

| Страница                                                              | Что рассматривается                                          |
|-----------------------------------------------------------------------|--------------------------------------------------------------|
| [Файловые дескрипторы](file_descriptors.md)                           | `open`, `read`, `write`, `close`, `lseek`, разреженные файлы |
| [Перенаправление ввода/вывода](input_output_redirection.md)           | `dup`, `dup2`, перенаправление stdin/stdout/stderr, `tee`    |
| [Основы файловых систем](filesystems_basics.md)                       | Inode, суперблок, VFS, типы файлов, виртуальные ФС           |
| [Операции с директориями](directory_operations.md)                    | `opendir`, `readdir`, `stat`, `fstat`, `fstatat`             |
| [Команды mv и rm](mv_and_rm_commands.md)                              | `rename`, `unlink`, `rmdir`, атомарность, счётчик ссылок     |
| [Жёсткие и символические ссылки](hard_and_symbolic_links.md)          | `link`, `symlink`, `readlink`, различия и ограничения        |
| [Права доступа и атрибуты файлов](file_permissions_and_attributes.md) | `chmod`, `chown`, SUID/SGID/sticky, атрибуты ext4            |
| [Открытые файлы и процессы](open_files_and_processes.md)              | `/proc/<pid>/fd`, `lsof`, `fuser`, таблица открытых файлов   |
| [Блочные и символьные устройства](block_and_character_devices.md)     | major/minor номера, `/dev`, виртуальные устройства, `mknod`  |
| [OverlayFS и слои Docker](overlayfs.md)                               | union mount, lower/upper/work, copy-up, whiteouts, overlay2  |
| [Linux block layer](block_layer.md)                                   | bio, request queue, I/O schedulers, blk-mq, FUA, plugging    |
| [Внутреннее устройство ФС](filesystem_internals.md)                   | ext4 layout, extents, htree, журналирование, btrfs/XFS/ZFS   |
| [FUSE: filesystem в userspace](fuse.md)                               | libfuse high/low-level API, FUSE protocol, sshfs/gocryptfs/s3fs внутри, performance |
| [inotify и fanotify](inotify_fanotify.md)                             | watch descriptors, event masks, fanotify permission events, лимиты IDE/VSCode |
| [NVMe и SPDK](nvme_spdk.md)                                           | NVMe protocol (SQ/CQ, namespaces, ZNS), NVMe-oF, SPDK polled-mode driver, io_uring IOPOLL |
| [ZFS и Btrfs](zfs_btrfs.md)                                           | CoW filesystems, ZFS (vdev/zpool/ARC/ZIL/RAID-Z/send-receive), Btrfs (subvolumes/snapshots/reflinks) |
