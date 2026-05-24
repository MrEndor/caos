# Linux namespaces: фундамент контейнеров

Namespaces — механизм ядра Linux, изолирующий то, что процесс **видит**: свой список mount points, свою таблицу PID,
свой сетевой стек, свой hostname. Снаружи это всё ещё одно ядро и одна машина, но изнутри процесс верит, что он запущен
в собственной системе.

Без namespaces классический `chroot(2)` защищает только файловую систему: процесс, запертый в chroot-jail, всё равно
видит PID-ы соседей, может им сигналить, использует общий network stack и общий список UID-ов с host'ом. На namespaces
построены все контейнеры в Linux: Docker, LXC, Podman, systemd-nspawn, runc, containerd. Когда runc «создаёт
контейнер» — это последовательность `clone()` с правильными `CLONE_NEW*` флагами и `mount()`/`pivot_root()` поверх.

## Восемь типов namespaces

| Namespace | Флаг (clone/unshare/setns) | Изолирует                                            | Появился в ядре |
|-----------|----------------------------|------------------------------------------------------|-----------------|
| mount     | `CLONE_NEWNS`              | список mount points, propagation                     | 2.4.19 (2002)   |
| UTS       | `CLONE_NEWUTS`             | hostname, domainname                                 | 2.6.19          |
| IPC       | `CLONE_NEWIPC`             | System V IPC, POSIX message queues                   | 2.6.19          |
| PID       | `CLONE_NEWPID`             | таблица PID, init процесса (PID 1)                   | 2.6.24          |
| network   | `CLONE_NEWNET`             | интерфейсы, IP, routes, iptables, sockets, /proc/net | 2.6.29          |
| user      | `CLONE_NEWUSER`            | UID/GID mapping, capabilities                        | 3.8             |
| cgroup    | `CLONE_NEWCGROUP`          | корень иерархии cgroup                               | 4.6             |
| time      | `CLONE_NEWTIME`            | монотонные часы `CLOCK_MONOTONIC`, `CLOCK_BOOTTIME`  | 5.6             |

Флаг `CLONE_NEWNS` назван так, потому что это был **самый первый** namespace в Linux (mount) — тогда слово «namespace»
ещё не было общим термином. Все остальные флаги начинаются с `CLONE_NEW<имя>`.

```
┌─────────────────────────────────────────────────────────────────┐
│                  один Linux kernel, одна машина                 │
│                                                                 │
│  ┌──────────────────┐  ┌──────────────────┐  ┌───────────────┐  │
│  │   контейнер A    │  │   контейнер B    │  │     host      │  │
│  │ ─────────────────│  │ ─────────────────│  │ ──────────────│  │
│  │ mnt:  своё /     │  │ mnt:  своё /     │  │ mnt:  /       │  │
│  │ pid:  init=1     │  │ pid:  init=1     │  │ pid:  systemd │  │
│  │ net:  eth0=10... │  │ net:  eth0=10... │  │ net:  eth0,wl │  │
│  │ uts:  app-1      │  │ uts:  db-2       │  │ uts:  worksta │  │
│  │ user: root↔1000  │  │ user: root↔1001  │  │ user: real    │  │
│  │ ipc:  своя SysV  │  │ ipc:  своя SysV  │  │ ipc:  общий   │  │
│  │ cgrp: /          │  │ cgrp: /          │  │ cgrp: реальный│  │
│  │ time: своё mono  │  │ time: своё mono  │  │ time: реальное│  │
│  └──────────────────┘  └──────────────────┘  └───────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

Namespaces ортогональны cgroups. Namespaces отвечают на вопрос «**что видит** процесс», cgroups — «**сколько ресурсов**
ему позволено потребить». Контейнер — это процесс с собственными namespaces, помещённый в cgroup с лимитами.

## Три способа создать/войти в namespace

### clone(CLONE_NEW*)

`clone(2)` создаёт новый процесс. Если передать флаги `CLONE_NEW*`, ребёнок появится сразу в новых namespaces, родитель
остаётся в старых.

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

static char stack[1 << 20];

static int child(void *arg) {
    sethostname("container", 9);
    execl("/bin/sh", "sh", NULL);
    return 1;
}

int main(void) {
    int flags = CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;
    pid_t pid = clone(child, stack + sizeof stack, flags, NULL);
    waitpid(pid, NULL, 0);
    return 0;
}
```

### unshare(CLONE_NEW*)

`unshare(2)` отделяет **текущий** процесс в новые namespaces, не порождая ребёнка. Удобно для shell-сессий и
интерактивных
экспериментов.

```bash
# войти в новый UTS namespace и сменить hostname только для себя
sudo unshare --uts /bin/bash
hostname isolated
hostname            # → isolated (только в этом bash)
# в другом терминале — старый hostname
```

`CLONE_NEWPID` через `unshare` устроен особенно: текущий процесс **не** становится PID 1, в новый PID ns попадают только
его потомки (нужен fork сразу после). Поэтому `unshare --pid --fork` идёт почти всегда вместе.

### setns(fd, type)

`setns(2)` присоединяет текущий процесс к **существующему** namespace, открытому через файловый дескриптор. Дескриптор
получается из `/proc/PID/ns/<тип>`.

```c
int fd = open("/proc/1234/ns/net", O_RDONLY);
setns(fd, CLONE_NEWNET);   // теперь у нас network ns процесса 1234
```

Это позволяет «зайти внутрь» контейнера и видеть его сеть/файлы.

### Shell-инструменты

```bash
# создать новый ns и сразу выполнить команду
unshare --net --pid --fork --mount-proc /bin/bash

# войти в namespaces работающего контейнера
nsenter --target $(pgrep -n nginx) --mount --uts --ipc --net --pid /bin/sh

# заглянуть в network ns конкретного контейнера Docker
PID=$(docker inspect -f '{{.State.Pid}}' my-app)
nsenter -t $PID -n ip addr
```

## /proc/PID/ns: namespace как файл

Каждому namespace ядро даёт уникальный inode. Symlink в `/proc/PID/ns/<type>` указывает на «магический» файл вида
`mnt:[4026531840]` — это inode mount namespace, в котором живёт процесс.

```
$ ls -l /proc/self/ns/
lrwxrwxrwx mnt    -> 'mnt:[4026531840]'
lrwxrwxrwx pid    -> 'pid:[4026531836]'
lrwxrwxrwx net    -> 'net:[4026531992]'
lrwxrwxrwx user   -> 'user:[4026531837]'
lrwxrwxrwx uts    -> 'uts:[4026531838]'
lrwxrwxrwx ipc    -> 'ipc:[4026531839]'
lrwxrwxrwx cgroup -> 'cgroup:[4026531835]'
lrwxrwxrwx time   -> 'time:[4026531834]'
```

Если у двух процессов одинаковый inode для конкретного namespace — они в одном и том же namespace.

```
host shell (PID 2001)             container init (PID 5000)
  /proc/2001/ns/                    /proc/5000/ns/
  ├── mnt:[4026531840]              ├── mnt:[4026532418]   ◀── разные
  ├── net:[4026531992]              ├── net:[4026532555]   ◀── разные
  ├── pid:[4026531836]              ├── pid:[4026532500]   ◀── разные
  └── user:[4026531837] ──┬──────── └── user:[4026531837]  ◀── одинаковые!
                          │
                          └─── оба процесса в одном user ns
                               (контейнер без user remapping)
```

Namespace продолжает существовать, пока на него ссылается хотя бы один файловый дескриптор или живёт процесс внутри.
Это даёт трюк «удержать» пустой ns:

```bash
# bind-mount файла ns куда-нибудь — ns не исчезнет даже без процессов
mount --bind /proc/$$/ns/net /var/run/netns/myns
ip netns exec myns ip addr     # ip netns под капотом так и работает
```

## Mount namespace

Mount ns изолирует **список mount points**, видимый процессу. При создании нового mount ns ядро **копирует** текущий
список, дальше изменения распространяются по правилам propagation.

### Propagation types

Каждой точке монтирования назначен один из четырёх propagation типов. Они определяют, как mount/umount события
передаются между mount namespaces.

| Тип             | Флаг                | Что делает                                  |
|-----------------|---------------------|---------------------------------------------|
| `MS_SHARED`     | `--make-shared`     | mount/umount распространяются в обе стороны |
| `MS_PRIVATE`    | `--make-private`    | полная изоляция                             |
| `MS_SLAVE`      | `--make-slave`      | принимает события от master, свои не отдаёт |
| `MS_UNBINDABLE` | `--make-unbindable` | как private + нельзя bind-mount'ить         |

```
shared propagation (default в systemd):

  host mount ns                    container mount ns
  ┌──────────────────┐             ┌──────────────────┐
  │ /                │             │ /                │
  │ /home            │ ◀──────────▶│ /home            │
  │ /mnt/usb (new) ──┼────────────▶│ /mnt/usb (auto)  │
  └──────────────────┘             └──────────────────┘
        mount событие распространяется в обе стороны


slave propagation (рекомендуется для контейнеров):

  host mount ns                    container mount ns
  ┌──────────────────┐             ┌──────────────────┐
  │ /                │             │ /                │
  │ /home            │ ────────────▶│ /home (copy)    │
  │ /mnt/usb (new) ──┼────────────▶│ /mnt/usb (auto)  │
  │                  │             │ /private (new)   │  ◀── не уйдёт в host
  └──────────────────┘             └──────────────────┘
        host → container: да    container → host: нет


private propagation:

  host mount ns                    container mount ns
  ┌──────────────────┐             ┌──────────────────┐
  │ /                │             │ /                │
  │ /mnt/usb (new) ──┤             ├── /private (new) │
  └──────────────────┘             └──────────────────┘
        полная независимость, никакая сторона не видит изменений другой
```

До ядра 2.6.15 любой mount в новом mount ns был приватным, и Docker bind-mount'ы из host'а внутрь контейнера не
обновлялись бы при изменениях на хосте. Сейчас по умолчанию systemd ставит `/` в `MS_SHARED`, и контейнер-рантаймы
явно делают `mount --make-rslave /` при старте контейнера.

### Пример: приватный /tmp для одного процесса

```bash
# tmpfs виден только в этом unshare'нутом bash
sudo unshare --mount --propagation private /bin/bash
mount -t tmpfs none /tmp
echo secret > /tmp/file
ls /tmp                  # → file

# в другом терминале — старый /tmp без secret
```

### chroot vs mount namespace vs pivot_root

`chroot(2)` меняет «корень» процессу, но **не** трогает mount table. Процесс всё ещё видит mount points host'а через
`/proc/mounts`, может выйти за chroot, имея `CAP_SYS_CHROOT`.

`pivot_root(2)` атомарно меняет местами старый и новый корни всей mount table. Используется в контейнерах: образ
монтируется в новый mount ns, делается `pivot_root` в него, старый корень отмонтируется. После этого процесс физически
не может «вылезти» — старого корня уже нет в mount table.

```
runc создаёт контейнер:

  1. unshare(CLONE_NEWNS)                                — свой mount ns
  2. mount("rootfs.ext4", "/var/run/runc/abc/rootfs")    — образ как новый /
  3. mount --rbind /proc, /sys, /dev, /tmp               — заполнить
  4. pivot_root("/var/run/runc/abc/rootfs", ".old")      — атомарный swap
  5. umount(".old", MNT_DETACH)                          — отрезать прошлое
  6. chdir("/")                                          — корень установлен
```

## PID namespace

Внутри PID ns нумерация процессов начинается с 1. Первый процесс ns становится **init процессом** этого ns: он получает
PID 1, наследует обязанности reaper'а зомби и обработки сигналов от ядра.

```
host PID namespace                  container PID namespace
┌──────────────────────────┐
│ PID 1   systemd          │
│ PID 832 dockerd          │
│ PID 2001 containerd-shim │
│ PID 2050 nginx ──────────┼──── тот же процесс ────┐
│ PID 2052 worker1 ────────┼─── те же процессы ─────┤
│ PID 2053 worker2 ────────┼────────────────────────┤
└──────────────────────────┘                        ▼
                                    ┌──────────────────────────┐
                                    │ PID 1 nginx              │
                                    │ PID 2 worker1            │
                                    │ PID 3 worker2            │
                                    └──────────────────────────┘
```

Один и тот же процесс имеет **разные PID** в разных namespaces — это PID translation, его делает ядро при чтении
`/proc`. `getpid(2)` внутри процесса всегда возвращает PID **в текущем** ns. Чтобы узнать «настоящий» PID на хосте,
надо прочитать `/proc/PID/status`, поле `NSpid:` — там список PID-ов во всех вложенных ns.

### Свойства init процесса (PID 1)

- Сигналы, для которых **нет** установленного handler'а, игнорируются — кроме `SIGKILL` и `SIGSTOP`. Это означает, что
  `kill 1` из контейнера не убьёт его init, если в коде нет `signal(SIGTERM, ...)`.
- Когда init процесс умирает, ядро посылает `SIGKILL` всем остальным процессам этого ns и сам ns уничтожается.
- Любой осиротевший процесс в ns reparent'ится на init этого ns (а не на init хоста, как было бы без PID ns).

Это причина, по которой простой `CMD ["python", "app.py"]` в Docker — плохая идея: Python становится PID 1, не умеет
reap'ать зомби и плохо обрабатывает сигналы. Решения: `tini`, `dumb-init`, или флаг `docker run --init`.

### Nested PID namespaces

PID ns могут быть вложенными. Процесс в N-ом вложенном ns имеет N+1 PID: по одному в каждом ns выше плюс свой
собственный.

```
        host ns
        ┌──────────────────┐
        │ PID 3000         │ ◀──── видно везде выше
        └──────┬───────────┘
               │
        container ns (level 1)
        ┌──────▼───────────┐
        │ PID 42    ◀──── видно в L1 и host
        └──────┬───────────┘
               │
        sandbox ns (level 2, внутри L1)
        ┌──────▼───────────┐
        │ PID 1     ◀──── это «настоящий» PID для процесса
        └──────────────────┘

один процесс — три разных PID:
  NSpid: 3000 42 1
```

Процесс из родительского ns может слать сигналы процессам в дочернем (по их PID в родительском ns), но не наоборот.

## User namespace

Самый молодой и сложный namespace. Изолирует UID/GID — внутри ns пользователь может быть root (UID 0), а снаружи это
обычный непривилегированный пользователь.

### UID/GID mapping

Mapping задаётся через файлы `/proc/PID/uid_map` и `/proc/PID/gid_map`. Каждая строка: `inside outside count` —
непрерывный диапазон.

```
$ cat /proc/$$/uid_map
         0       1000          1     ← root внутри ns ↔ UID 1000 снаружи
         1     100000      65536     ← UID 1..65536 внутри ↔ 100000..165535 снаружи
```

Преобразование двустороннее. Когда процесс из user ns создаёт файл, на диск пишется его **снаружный** UID. Когда читает
ownership файла — ядро транслирует обратно.

```
inside user ns                         outside (host)
┌──────────────────┐                   ┌──────────────────┐
│ UID 0   (root)   │  ──── mapping ──▶ │ UID 1000 (egor)  │
│ UID 1   (daemon) │  ──── mapping ──▶ │ UID 100001       │
│ UID 1000 (app)   │  ──── mapping ──▶ │ UID 101000       │
└──────────────────┘                   └──────────────────┘

  touch /tmp/file                      ls -l /tmp/file
  → owner: root (UID 0)                → owner: egor (UID 1000)
```

### Capabilities в user ns

Внутри user ns процесс получает **полный набор capabilities** относительно ресурсов, созданных внутри ns. Это позволяет
обычному пользователю настраивать сеть, монтировать tmpfs/proc, запускать chroot — всё внутри своего ns, без угрозы
хосту.

Однако capabilities не дают магических прав на ресурсы **родительского** ns. Root в дочернем user ns не может
прочитать `/etc/shadow` хоста, потому что для VFS он всё ещё UID 1000 (после translation).

### Rootless containers

User ns — основа rootless контейнеров: Podman, rootless Docker, Buildah. Обычный пользователь запускает контейнер,
внутри которого видит root, но на хосте контейнер бежит под его UID. Ему не нужен Docker daemon с правами root, не нужен
SUID, не нужен sudo.

```bash
# создать user ns с маппингом root↔себя
unshare -U -r /bin/bash
id              # uid=0(root) gid=0(root) внутри
mount -t tmpfs none /mnt   # работает! привилегии есть внутри ns
```

Флаг `-r` (`--map-root-user`) — сокращение для маппинга `0 <real-uid> 1`. Для полноценного маппинга диапазона
(`subuid`/`subgid`) Podman читает `/etc/subuid`, `/etc/subgid`.

### Безопасность

User namespaces исторически были источником большого количества CVE (CVE-2018-18955, CVE-2022-0185, CVE-2022-34918,
CVE-2023-32233 и другие): они открывают непривилегированным пользователям доступ к коду ядра, который раньше был
достижим только из root. Многие дистрибутивы (Debian, Ubuntu) ставят:

```bash
# полностью запретить непривилегированный clone user ns
sysctl kernel.unprivileged_userns_clone=0

# Ubuntu 24+ — apparmor profile вместо полного запрета
sysctl kernel.apparmor_restrict_unprivileged_userns=1
```

## Network namespace

Network ns — полностью изолированный сетевой стек: свои interfaces, IP-адреса, routing table, iptables/nftables rules,
`/proc/net`, sockets. Сокет, открытый в одном netns, недоступен из другого даже по тому же порту.

По умолчанию свежесозданный netns содержит только `lo` (loopback), и тот в state DOWN.

```bash
sudo ip netns add ns1
sudo ip netns exec ns1 ip link
# 1: lo: <LOOPBACK> mtu 65536 state DOWN
```

### veth pair

Чтобы соединить netns с внешним миром, ядро предоставляет **veth (virtual ethernet)** — пару виртуальных интерфейсов,
соединённых трубой: всё, что записано на один конец, выходит на другом.

```
host netns                                  container netns
┌─────────────────────┐                     ┌─────────────────────┐
│                     │                     │                     │
│    veth0 ◀══════════│═════════════════════│══════════▶ veth1    │
│ 192.168.1.1/24      │     виртуальная     │  192.168.1.2/24     │
│                     │       труба         │                     │
└─────────────────────┘                     └─────────────────────┘

  ping 192.168.1.2  ──▶  пакет ──▶ veth0 ──▶ veth1 ──▶ доставка
```

### Bridge

Когда контейнеров много, у каждого свой veth pair, и все нужно объединить в подсеть. Решение — **bridge** (виртуальный
коммутатор) на host'е. Docker создаёт bridge `docker0`, и host-конец каждого veth подключается к нему.

```
                  host netns
        ┌─────────────────────────────────┐
        │     bridge docker0 (10.0.0.1)   │
        │     ┌──────┬──────┬──────┐      │
        │     │veth0a│veth0b│veth0c│      │
        │     └───╥──┴───╥──┴───╥──┘      │
        └─────────╫──────╫──────╫─────────┘
                  ║      ║      ║
       veth pair  ║      ║      ║
                  ║      ║      ║
        ┌─────────╨───┐ ╔╝   ┌──╨──────────┐
        │  container1 │ ║    │  container3 │
        │ veth1       │ ║    │ veth3       │
        │ 10.0.0.2    │ ║    │ 10.0.0.4    │
        └─────────────┘ ║    └─────────────┘
                        ║
              ┌─────────╨───┐
              │  container2 │
              │ veth2       │
              │ 10.0.0.3    │
              └─────────────┘

bridge коммутирует L2-фреймы между всеми contained'ами + uplink через NAT
```

### Пример: ручная сборка netns с veth

```bash
# создать namespace
sudo ip netns add ns1

# создать veth pair
sudo ip link add veth0 type veth peer name veth1

# один конец оставить на host, второй — в ns1
sudo ip link set veth1 netns ns1

# поднять и настроить host-конец
sudo ip addr add 192.168.1.1/24 dev veth0
sudo ip link set veth0 up

# поднять и настроить container-конец
sudo ip netns exec ns1 ip addr add 192.168.1.2/24 dev veth1
sudo ip netns exec ns1 ip link set veth1 up
sudo ip netns exec ns1 ip link set lo up

# проверка
sudo ip netns exec ns1 ping -c 2 192.168.1.1
```

### Команды

```bash
ip netns add ns1                          # создать
ip netns list                             # список
ip netns exec ns1 <cmd>                   # выполнить в ns
ip netns delete ns1                       # удалить
ip -n ns1 addr                            # сокращение для exec ns1 ip addr
```

`ip netns` под капотом bind-mount'ит `/proc/PID/ns/net` в `/var/run/netns/<name>` — благодаря этому ns живёт без
процессов внутри.

## UTS, IPC, cgroup, time

**UTS** изолирует hostname (`gethostname`/`sethostname`) и NIS domainname. Используется в каждом контейнере, потому что
у каждого должен быть свой hostname. Самый дешёвый namespace.

**IPC** изолирует System V IPC objects (semaphores, shared memory segments, message queues) и POSIX message queues
(`/dev/mqueue`). Без него процессы из разных контейнеров могли бы видеть IPC-объекты друг друга и обмениваться данными
через `shmget`/`msgget`.

**cgroup** изолирует **видимый** корень иерархии cgroup. Процесс не видит, в какой реальный cgroup на хосте он помещён,
видит только относительный путь начиная со своего корня. Полезно, чтобы образ контейнера не зависел от структуры cgroup
host'а. Часто создаётся **последним**, уже после `pivot_root`, чтобы `/sys/fs/cgroup` уже был смонтирован.

**time** (самый новый, 5.6+) изолирует **смещения** для `CLOCK_MONOTONIC` и `CLOCK_BOOTTIME`. `CLOCK_REALTIME` (
системное
время) не изолирован — оно общее. Применение: миграция контейнеров (CRIU) с сохранением uptime, тестирование с
управляемым «возрастом» системы.

## Из чего собирается контейнер

Когда `docker run` или `podman run` запускает контейнер, под капотом исполняется примерно такая последовательность.
Реальный контейнер собирает runc/crun по OCI runtime spec.

```
runc create:

  1. clone(CLONE_NEWPID|CLONE_NEWNS|CLONE_NEWNET|
           CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWUSER)
       └─ ребёнок появляется во всех новых namespaces сразу

  2. в ребёнке:
       a. write /proc/self/uid_map   ◀── установить user mapping
          write /proc/self/gid_map
          (perm на gid_map требует /proc/self/setgroups = "deny")

       b. mount("/", "/", MS_PRIVATE|MS_REC)        — отрезать propagation от host
          mount(image_rootfs, "/run/rootfs", BIND)  — образ как новый /

       c. mount tmpfs, proc, sysfs, devpts, mqueue в новый rootfs
          mount --rbind /dev/null /dev/null  и т.п.

       d. pivot_root("/run/rootfs", "/run/rootfs/.old")
          umount("/run/rootfs/.old", MNT_DETACH)
          chdir("/")

       e. unshare(CLONE_NEWCGROUP)   — корень cgroup стал контейнерным

       f. sethostname("container-id")
          set rlimits, oom_score_adj

       g. apply seccomp filter
          drop capabilities (keep только minimal set)

       h. setgroups, setgid, setuid в маппированный «root внутри ns»

       i. execve("/entrypoint")
```

Параллельно cgroup-контроллер на хосте помещает PID контейнера в нужный cgroup с лимитами CPU/memory.

## Подводные камни

**User ns заблокирован дистрибутивом.** Если rootless container не запускается с ошибкой `clone: Operation not
permitted` — проверить `sysctl kernel.unprivileged_userns_clone`. На Ubuntu 24+ — ещё и AppArmor (`dmesg | grep
userns`).

**Mount propagation ломает volume mount.** Docker volume должен пробрасываться из host в контейнер. Если корень
контейнера в `MS_PRIVATE`, последующие host mounts не попадают внутрь. Решение — `mount --make-rslave /` после `unshare
--mount`, как делает runc.

**Zombies в PID ns.** В контейнере, где init — обычное приложение (Node.js, Python, java), осиротевшие дети не
reap'аются: приложение не вызывает `wait()` по `SIGCHLD`. Zombies накапливаются, kernel жалуется в dmesg. Использовать
`tini`/`dumb-init` или `docker run --init`.

**SIGTERM игнорируется в PID ns.** Сигналы без handler'а ядро не доставляет PID 1 (защита от случайного `kill 1` в
обычной системе). Если приложение должно gracefully останавливаться по `docker stop`, оно обязано установить
`SIGTERM` handler — либо снова, обернуть `tini` поверх.

**Порты не «выставляются» сами.** Контейнер в своём netns не доступен снаружи host'а. Нужен либо userspace proxy
(`docker-proxy`), либо NAT через iptables (`-A PREROUTING -p tcp --dport 8080 -j DNAT --to 10.0.0.2:80`). Docker по
умолчанию делает оба варианта.

**Network ns теряется, если в нём нет процессов.** Если netns создан только через `clone`, без bind-mount'а в
`/var/run/netns/`, при завершении последнего процесса ns исчезнет вместе с интерфейсами.

## Tooling

```bash
# список всех namespaces в системе, их типы, владельцы, процессы
lsns
lsns -t net                          # только network ns
lsns -p $$                           # ns, в которых сидит текущий shell

# войти в namespaces работающего процесса
nsenter --target 1234 --all          # все ns процесса 1234
nsenter -t 1234 -n ip addr           # только network ns

# создать свой ns и выполнить
unshare --user --map-root-user --mount --pid --fork --mount-proc /bin/bash

# управление netns
ip netns list / add / delete / exec

# готовый «лёгкий контейнер» из systemd
systemd-nspawn -D /var/lib/machines/debian
machinectl shell <name>
```

## Связанные темы

- [cgroups: углублённо](cgroups.md) — ограничение ресурсов; namespaces изолируют видимость, cgroups — потребление
- [fork и exec](../processes/fork_and_exec.md) — `clone(2)` с флагами `CLONE_NEW*`, на котором всё построено
- [Реализация потоков (clone)](../threads/thread_implementation_clone.md) — другие флаги `CLONE_*` для разделения
  адресного пространства, FS, файлов
- [seccomp](seccomp.md) — фильтр syscall'ов, комбинируется с namespaces в каждом контейнере
- [Приоритеты, affinity, capabilities](../processes/process_priority_affinity_capabilities.md) — capabilities внутри
  user namespace

## Источники

- `man 7 namespaces`, `man 7 mount_namespaces`, `man 7 pid_namespaces`, `man 7 user_namespaces`,
  `man 7 network_namespaces`
- `man 2 clone`, `man 2 unshare`, `man 2 setns`, `man 2 pivot_root`
- [Namespaces in operation — Michael Kerrisk, LWN](https://lwn.net/Articles/531114/)
- [User namespaces — LWN](https://lwn.net/Articles/532593/)
- [Mount namespaces and shared subtrees — LWN](https://lwn.net/Articles/689856/)
- [OCI Runtime Specification](https://github.com/opencontainers/runtime-spec/blob/main/runtime.md)
