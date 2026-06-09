# OverlayFS: union mount и слои Docker

OverlayFS — это **union mount** файловая система: несколько каталогов на разных носителях склеиваются в один логический
вид. Запись идёт в один отдельный слой, не трогая остальные. Именно это свойство сделало OverlayFS стандартным storage
driver для Docker, containerd и Podman: образ контейнера хранится один раз, а каждый запущенный контейнер получает свой
писательский слой поверх него.

Изначально union mount в Linux реализовывала **aufs** (Another Union FS), но её патчи так и не приняли в mainline ядро.
OverlayFS написана Miklos Szeredi (автор FUSE), вошла в ядро в Linux 3.18 (2014) и быстро вытеснила aufs.

## Что такое union mount

Union mount берёт несколько каталогов и показывает их как один:

```
  /lower1                /lower2                /upper
  ┌──────────┐           ┌──────────┐           ┌──────────┐
  │ a.txt    │           │ b.txt    │           │ c.txt    │
  │ doc.pdf  │           │ doc.pdf  │           │ b.txt    │
  └──────────┘           └──────────┘           └──────────┘
       │                      │                      │
       └──────────────────────┼──────────────────────┘
                              ▼
                       /merged (overlay)
                       ┌──────────────┐
                       │ a.txt        │  ← из lower1
                       │ b.txt        │  ← из upper (перекрывает lower2)
                       │ c.txt        │  ← из upper
                       │ doc.pdf      │  ← из lower1 (перекрывает lower2)
                       └──────────────┘
```

Lookup идёт **сверху вниз**: сначала upper, затем lower-каталоги в порядке перечисления. Первое совпадение выигрывает.
Запись и удаление меняют только upper — lower остаётся неизменным, что и позволяет шарить его между процессами и
контейнерами.

## Терминология

OverlayFS оперирует четырьмя ролями каталогов:

| Каталог    | Назначение                               | Доступ          |
|------------|------------------------------------------|-----------------|
| `lowerdir` | read-only слой (или цепочка слоёв)       | только чтение   |
| `upperdir` | writable слой                            | чтение и запись |
| `workdir`  | служебный каталог для атомарных операций | внутреннее ядро |
| `merged`   | точка монтирования, видимый результат    | чтение и запись |

**lowerdir** — нижний слой. Можно перечислить несколько через `:` — тогда они образуют стек, где левый перекрывает
правые. Левый ближе к upper, правый — самый «древний».

**upperdir** — единственный слой, в который OverlayFS пишет. Все изменения, создания и удаления складируются здесь.

**workdir** — каталог в той же файловой системе, что и upperdir. Ядро использует его для подготовки файлов при copy-up
и whiteout — операции должны быть атомарны через `rename(2)`, а атомарный rename работает только в пределах одной ФС.
Workdir должен быть пустым на момент монтирования и не должен пересекаться с upperdir.

**merged** — точка монтирования, через которую пользователь видит объединённый результат.

## Mount синтаксис

Монтирование вручную:

```bash
mount -t overlay overlay \
      -o lowerdir=/lower1:/lower2:/lower3,upperdir=/upper,workdir=/work \
      /merged
```

Что важно:

- `lowerdir=A:B:C` — A перекрывает B, B перекрывает C. Цепочка читается слева направо.
- `upperdir` и `workdir` опциональны: без них получится read-only overlay (можно объединить только lower-слои).
- `workdir` обязан быть на той же ФС, что и `upperdir`.
- `merged` не должен совпадать ни с одним из lower/upper/workdir.

Размонтирование — обычный `umount /merged`. Lower и upper при этом не пропадают: они продолжают существовать как
обычные каталоги.

## Lookup: как файл находится

При обращении к файлу через merged ядро ищет его сначала в upper, потом в lowerdir-слоях по порядку:

```
  read /merged/foo.txt
        │
        ▼
  ┌─────────────────────────────────────────────────┐
  │ upper/foo.txt существует?                       │
  └──────┬──────────────────────────────────┬───────┘
         │ да                               │ нет
         ▼                                  ▼
   возвращаем upper/foo.txt        ┌───────────────────────────────┐
                                   │ lower1/foo.txt существует?    │
                                   └────┬──────────────────────┬───┘
                                        │ да                   │ нет
                                        ▼                      ▼
                                  возвращаем lower1/foo.txt    ...
                                                              (дальше lower2, lower3)
```

Если файл не найден ни в одном слое — `ENOENT`. Если в upper лежит whiteout (см. ниже) — поиск тоже завершается с
`ENOENT`, даже если файл есть в lower.

## Copy-up

Файлы в lower нельзя менять: они read-only. Когда пользователь открывает lower-файл на запись или вызывает `chmod`,
`chown`, `truncate`, OverlayFS делает **copy-up** — копирует файл целиком в upper и дальше работает с копией.

```
  write /merged/big.bin
        │
        ▼
  ┌────────────────────────────────────────────────────┐
  │ big.bin лежит в lower → нужно copy-up              │
  └────────────────────────┬───────────────────────────┘
                           │
                           ▼
            ┌──────────────────────────────────┐
            │ 1. создать копию в workdir       │
            │    (атомарность операции)        │
            ├──────────────────────────────────┤
            │ 2. скопировать содержимое        │
            │    lower/big.bin → workdir/tmp   │
            ├──────────────────────────────────┤
            │ 3. перенести метаданные:         │
            │    mode, uid/gid, xattrs, time   │
            ├──────────────────────────────────┤
            │ 4. rename(workdir/tmp,           │
            │           upperdir/big.bin)      │
            ├──────────────────────────────────┤
            │ 5. выполнить write поверх        │
            │    upperdir/big.bin              │
            └──────────────────────────────────┘
```

Copy-up — основной источник задержек в OverlayFS. Первая запись в 10-гигабайтный файл из lower означает копирование
всех 10 GB на диск (или хотя бы в page cache), даже если меняется один байт.

Granularity copy-up — **весь файл целиком**. OverlayFS не умеет копировать «по странице» или хранить дельты на блочном
уровне — для этого существуют CoW-файловые системы (btrfs, ZFS), но это другой подход.

С Linux 4.19 появился **metadata-only copy-up**: при операциях, меняющих только метаданные (`chmod`, `chown`,
`utimensat`), содержимое файла не копируется — в upper создаётся пустой stub-файл с обновлёнными метаданными и xattr,
указывающим на оригинал в lower. Контент читается из lower напрямую. Первая же запись данных триггерит полный copy-up.
Опция включается через `redirect_dir=on,metacopy=on`.

## Whiteouts: удаление файлов из lower

Удалить файл из lower физически невозможно — lower read-only. Чтобы пользователь видел «удаление», OverlayFS пишет в
upper специальный маркер — **whiteout**. В классической OverlayFS whiteout — это **character device с major=0,
minor=0**:

```bash
ls -l /upper/
# c--------- 1 root root 0, 0 ... foo.txt   ← whiteout: foo.txt «удалён»
```

```
  rm /merged/foo.txt
        │
        ▼
  ┌────────────────────────────────────────────┐
  │ foo.txt существует только в lower          │
  └────────────────────────┬───────────────────┘
                           ▼
          mknod(upper/foo.txt, S_IFCHR, makedev(0,0))
                           │
                           ▼
          ┌────────────────────────────────┐
          │ /upper                         │
          │   foo.txt  (char dev 0,0)      │  ← whiteout
          └────────────────────────────────┘

  Lookup /merged/foo.txt:
        upper/foo.txt → char dev 0,0 → возвращаем ENOENT,
        не идём в lower
```

Альтернативная форма — расширенный атрибут `trusted.overlay.whiteout` на обычном пустом файле, появилась для совместимости
с ФС, где `mknod` запрещён (например, в user namespace). Включается опцией `userxattr` (Linux 5.11+).

## Opaque directories

Что делать, если пользователь делает `rm -rf` каталога, у которого есть содержимое в lower? Поставить whiteout на каждый
файл изнутри — затратно и не работает для файлов, которых пользователь не видел. Решение — пометить каталог в upper как
**opaque** (непрозрачный):

```bash
setfattr -n trusted.overlay.opaque -v y /upper/dir
```

```
  Когда merged/dir помечен как opaque:
        │
        ▼
  ┌─────────────────────────────────────────────────┐
  │ readdir(/merged/dir)                            │
  │   игнорирует все lower/dir/*                    │
  │   показывает только upper/dir/*                 │
  └─────────────────────────────────────────────────┘
```

OverlayFS автоматически ставит opaque, когда пользователь:

1. Удаляет каталог из lower (`rm -rf /merged/dir`, затем `mkdir /merged/dir`).
2. Переименовывает каталог в lower при включённой опции `redirect_dir`.

Без opaque на свежесозданном пустом каталоге через merged всё ещё было бы видно содержимое старого lower/dir.

## Docker и слои образов

Docker использует OverlayFS (драйвер `overlay2`) для построения файловой системы контейнера из слоёв образа. Каждая
инструкция `RUN`, `COPY` или `ADD` в Dockerfile создаёт **новый слой** — каталог с дельтой относительно предыдущего:

```dockerfile
FROM debian:12          # слой 1: базовый rootfs
RUN apt-get update      # слой 2: обновлённый /var/lib/apt
RUN apt-get install -y nginx   # слой 3: nginx + зависимости
COPY app.conf /etc/nginx/      # слой 4: конфиг
CMD ["nginx", "-g", "daemon off;"]
```

При запуске контейнера Docker монтирует overlay из этих слоёв плюс новый upper:

```
  docker run nginx-image
        │
        ▼
  ┌──────────────────────────────────────────────────────────┐
  │                                                          │
  │  /var/lib/docker/overlay2/                               │
  │                                                          │
  │   layer4/diff  ← /etc/nginx/app.conf                     │
  │   layer3/diff  ← nginx и зависимости                     │
  │   layer2/diff  ← apt update                              │
  │   layer1/diff  ← debian:12 rootfs                        │
  │                                                          │
  │   container-xxx/diff   ← writable upper для контейнера   │
  │   container-xxx/work   ← workdir                         │
  │   container-xxx/merged ← rootfs контейнера               │
  │                                                          │
  └──────────────────────────────────────────────────────────┘

  Mount overlay:
     lowerdir = layer4/diff : layer3/diff : layer2/diff : layer1/diff
     upperdir = container-xxx/diff
     workdir  = container-xxx/work
     merged   = container-xxx/merged
```

Стек lowerdir строится снизу вверх (от старого слоя к новому), но при mount-команде указывается **сверху вниз**: левый —
самый верхний (последняя инструкция Dockerfile), правый — самый нижний (`FROM`-образ). Это соответствует семантике
OverlayFS: левый перекрывает правые.

Получаем ровно ту экономию, ради которой union mount и придуман:

- Образ хранится в overlay2 один раз.
- Сто контейнеров из одного образа добавляют только сто маленьких upperdir.
- Чтение nginx-бинарника всеми контейнерами идёт через один и тот же inode в page cache.

Посмотреть текущие overlay-mount'ы:

```bash
mount | grep overlay
findmnt -t overlay
```

Посмотреть структуру слоёв образа:

```bash
docker image inspect <image> --format '{{.GraphDriver.Data}}'
```

## Лимиты и нюансы

**Глубина стека.** Раньше OverlayFS ограничивала число lowerdir в 500, в современных ядрах — 500 на один mount и 128 в
случае nested overlay. Docker сжимает образы при сборке (`docker build --squash`), чтобы не упереться в лимит.

**Поведение `rename(2)` через слои.** Переименование файла из lower в новое место в merged по умолчанию делает copy-up
плюс whiteout — это дорого. Опция `redirect_dir=on` позволяет переименовать каталог без копирования содержимого, записав
в upper xattr `trusted.overlay.redirect` с путём оригинала.

**inode numbers.** Два процесса, открывшие один и тот же lower-файл из разных контейнеров, могут получить разные
`st_ino` (зависит от опции `xino` и того, был ли copy-up). Программы, считающие inode уникальными идентификаторами в
пределах процесса, на этом ломаются. Опция `xino=on` стабилизирует inode-номера, кодируя слой в верхние биты.

**fsync.** `fsync` файла после copy-up синхронизирует upper, но не гарантирует ничего про lower (которому и так синхрон
не нужен — он read-only).

**Page cache.** Lower-файл и его копия в upper после copy-up имеют **разные** inode и **разные** записи в page cache.
Память, потраченная на кеш lower-файла до copy-up, не освобождается автоматически.

## Альтернативы

| Решение          | Подход                            | Особенности                                   |
|------------------|-----------------------------------|-----------------------------------------------|
| `aufs`           | union mount                       | deprecated, не в mainline ядре                |
| `btrfs` subvol   | snapshot на уровне ФС             | CoW на блочном уровне, быстрые snapshots      |
| `zfs` clone      | snapshot + writable clone         | CoW + integrity checks + RAID, не в mainline  |
| `device-mapper`  | thin-provisioned LVM              | блочный CoW, использовался Docker до overlay2 |
| `fuse-overlayfs` | userspace-реализация поверх FUSE  | для rootless контейнеров без CAP_SYS_ADMIN    |

В современном Linux выбор почти всегда падает на OverlayFS: она в mainline, не требует специальной нижней ФС
(работает поверх ext4, XFS, btrfs), хорошо протестирована и быстра благодаря работе в kernel space.

## Диагностика

Проверить тип драйвера Docker:

```bash
docker info | grep -i storage
# Storage Driver: overlay2
```

Посмотреть слои конкретного контейнера:

```bash
docker inspect <container> --format '{{json .GraphDriver.Data}}' | jq
```

Whiteouts и opaque в upper:

```bash
# Найти whiteouts (char dev 0,0):
find /var/lib/docker/overlay2/<id>/diff -type c

# Найти opaque-каталоги:
getfattr -R -d -m trusted.overlay /var/lib/docker/overlay2/<id>/diff
```

## Связанные темы

- [Основы файловых систем](filesystems_basics.md) — VFS, inode, типы файлов; OverlayFS — VFS-уровневая ФС
- [Linux namespaces](../isolation/namespaces.md) — mount namespace, в котором живёт overlay контейнера
- [cgroups: углублённо](../isolation/cgroups.md) — ограничение ресурсов запущенного из overlay контейнера
- [Блочные и символьные устройства](block_and_character_devices.md) — char device 0,0 в роли whiteout

## Источники

- [overlayfs.rst — kernel.org Documentation](https://www.kernel.org/doc/Documentation/filesystems/overlayfs.rst)
- [Docker storage drivers — docs.docker.com](https://docs.docker.com/storage/storagedriver/overlayfs-driver/)
- `man 5 overlayfs` (где доступно)
- LWN: ["Overlay filesystem"](https://lwn.net/Articles/325369/)
- LWN: ["Metadata-only copy-up for overlayfs"](https://lwn.net/Articles/762355/)
