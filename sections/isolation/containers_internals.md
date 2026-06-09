# Внутреннее устройство контейнеров

Docker, Podman, containerd, Kubernetes — все они называют свои абстракции одним словом «контейнер», но в Linux ядре
никакой сущности «container» нет. Контейнер — это композиция трёх независимых kernel-механизмов:
**namespaces** (что процесс видит), **cgroups** (сколько ресурсов потребляет) и **OverlayFS** (откуда берётся
корневая файловая система). Сверху накручены пользовательские контракты: формат образа, реестр для распространения,
runtime для запуска, daemon для управления, scheduler для оркестрации. Когда осознаёшь, что Docker — не магия, а
оркестрация уже существовавших примитивов, исчезает мистика и появляются ответы на «почему оно ведёт себя именно так».

## Из чего собран контейнер

```
┌─────────────────────────────────────────────────────────────────────┐
│                       один Linux kernel                             │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                      контейнер                              │    │
│  │                                                             │    │
│  │  ┌───────────────┐  ┌───────────────┐  ┌──────────────────┐ │    │
│  │  │  namespaces   │  │    cgroups    │  │    OverlayFS     │ │    │
│  │  │               │  │               │  │                  │ │    │
│  │  │  mnt, pid,    │  │  cpu, memory, │  │  lowerdir = ro   │ │    │
│  │  │  net, uts,    │  │  io, pids,    │  │  upperdir = rw   │ │    │
│  │  │  ipc, user,   │  │  PSI, freezer │  │  workdir         │ │    │
│  │  │  cgroup, time │  │               │  │  merged = rootfs │ │    │
│  │  └───────────────┘  └───────────────┘  └──────────────────┘ │    │
│  │         │                  │                    │           │    │
│  │         └──────────────────┼────────────────────┘           │    │
│  │                            ▼                                │    │
│  │             ┌─────────────────────────────┐                 │    │
│  │             │  процесс entrypoint         │                 │    │
│  │             │  (для него — отдельная      │                 │    │
│  │             │   "машина" со своим init)   │                 │    │
│  │             └─────────────────────────────┘                 │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                     │
│  + seccomp filter, capabilities mask, AppArmor/SELinux profile      │
└─────────────────────────────────────────────────────────────────────┘
```

Заменить любой из трёх блоков можно отдельно. `systemd-nspawn` берёт namespaces + cgroups, но монтирует обычную
директорию как rootfs (без overlay). `firejail` использует namespaces + seccomp, но не использует cgroups и overlay.
LXC исторически работал поверх **bind-mount** rootfs, а не overlay. Docker — это конкретная связка, навязанная
популярностью. Понимая, что собирать можно как угодно, перестаёшь думать в категориях «Docker даёт» — начинаешь
думать в категориях «эти три kernel-фичи дают».

## OCI: стандартизация

До 2015 года Docker был и форматом образа, и runtime'ом, и daemon'ом, и реестром — всё в одном проприетарном
комплекте. Rkt от CoreOS пытался сделать альтернативу, но без стандарта это вело к фрагментации экосистемы. В июне
2015 Docker, CoreOS, Red Hat, Google и ещё ~20 компаний учредили **Open Container Initiative (OCI)** под крылом
Linux Foundation. OCI определяет три спецификации:

| Спецификация          | Что определяет                                              | Реализации                          |
|-----------------------|-------------------------------------------------------------|-------------------------------------|
| **image-spec**        | формат tarball-слоёв, manifest.json, config.json, digest    | Docker, Podman, BuildKit, Buildah   |
| **runtime-spec**      | формат runtime bundle (rootfs + config.json), API runtime'а | runc, crun, youki, gVisor, Kata     |
| **distribution-spec** | HTTP API реестра для push/pull/discovery                    | Docker Hub, Harbor, GHCR, Quay      |

Дополнительная **artifacts-spec** (с 2023) позволяет хранить в реестре не только образы, но и подписи (cosign),
SBOM, Helm chart'ы, WASM-модули. Это превратило OCI-реестр в универсальный content-addressable store.

```
   образ                                  bundle                          процесс
┌───────────┐  unpack  ┌───────────────────────────┐ runc create   ┌─────────────────┐
│  layers/  │ ───────▶ │  rootfs/  (распакованный) │ ────────────▶ │  PID, ns, cgrp, │
│  manifest │          │  config.json (runtime)    │               │  cap, seccomp   │
│  config   │          └───────────────────────────┘               └─────────────────┘
│ (image-   │                       ▲                                      ▲
│  spec)    │                       │                                      │
└───────────┘                       │                                      │
      ▲                             │                                      │
      │                             │                                      │
   registry                  containerd /                              kernel
 (distribution-spec)         dockerd прокидывает                    (namespaces +
                             OCI image → OCI bundle                  cgroups + ovl)
```

Три спецификации формально независимы. На практике все три реализуются одним и тем же набором инструментов:
containerd понимает image-spec, генерирует bundle, дёргает runc для runtime-spec, а скачивает всё с реестра
по distribution-spec.

## OCI image format

Образ — это не «диск с операционной системой», как можно подумать по аналогии с VM. Это набор tar-архивов плюс
два JSON-файла, описывающих как они складываются и что с ними делать.

```
my-image.tar (unpacked):
┌──────────────────────────────────────────────────────────────┐
│  oci-layout                  ← маркер формата                │
│  index.json                  ← список manifest'ов            │
│                              (multi-arch: amd64, arm64...)   │
│  blobs/sha256/                                               │
│  ├── 0a1b2c... (config.json) ← runtime-config: cmd, env, ws  │
│  ├── 4d5e6f... (manifest)    ← список слоёв + конфиг         │
│  ├── 7890ab... (layer 0)     ← tarball: базовая ОС           │
│  ├── cdef01... (layer 1)     ← tarball: установка пакетов    │
│  ├── 234567... (layer 2)     ← tarball: COPY app             │
│  └── 89abcd... (layer 3)     ← tarball: COPY config          │
└──────────────────────────────────────────────────────────────┘
```

Каждый слой — обычный tar (опционально gzip), содержащий **diff** относительно предыдущего: новые файлы плюс
файлы-маркеры удалений (`.wh.foo` означает «удалить foo из нижележащих слоёв»). Идентификатор слоя — SHA-256 от
его содержимого. Это **content-addressable**: одинаковый слой в двух разных образах хранится один раз.

### manifest.json

```json
{
  "schemaVersion": 2,
  "mediaType": "application/vnd.oci.image.manifest.v1+json",
  "config": {
    "mediaType": "application/vnd.oci.image.config.v1+json",
    "size": 1457,
    "digest": "sha256:0a1b2c..."
  },
  "layers": [
    { "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
      "size": 78462913, "digest": "sha256:7890ab..." },
    { "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
      "size":   245013, "digest": "sha256:cdef01..." }
  ]
}
```

Manifest — это рецепт: какие слои в каком порядке распаковать и какой config к ним приложить. Сам не содержит
данных, только указатели на blob'ы по digest.

### config.json (image config, не путать с runtime config)

```json
{
  "architecture": "amd64", "os": "linux",
  "config": {
    "Env": ["PATH=/usr/local/bin:/usr/bin"],
    "Cmd": ["/app/server"],
    "WorkingDir": "/app",
    "ExposedPorts": { "8080/tcp": {} },
    "User": "1000:1000"
  },
  "rootfs": {
    "type": "layers",
    "diff_ids": [
      "sha256:111...",   ← tar-digest БЕЗ gzip
      "sha256:222..."
    ]
  },
  "history": [ { "created": "...", "created_by": "RUN apt-get install" } ]
}
```

Поле `rootfs.diff_ids` — это digest'ы **распакованных** tar'ов (а `layers[].digest` в manifest — digest'ы сжатых
blob'ов). Двойной digest нужен потому, что при подписи и доверии важно содержимое после распаковки, а при передаче
по сети — байтовое содержимое архива.

### distribution-spec: pull

`docker pull alpine:latest` под капотом — серия HTTP-запросов к реестру:

```
1. GET /v2/                              ← auth challenge, узнаём scheme
2. GET /v2/library/alpine/manifests/latest
                                         ← по тегу → manifest digest
3. GET /v2/library/alpine/manifests/sha256:<digest>
                                         ← manifest.json (список слоёв)
4. GET /v2/library/alpine/blobs/sha256:<config-digest>
                                         ← image config
5. GET /v2/library/alpine/blobs/sha256:<layer-1-digest>
   GET /v2/library/alpine/blobs/sha256:<layer-2-digest>
   ...                                   ← каждый слой отдельно,
                                            параллельно, с возобновлением
```

Каждый шаг возвращает либо JSON, либо blob с заголовком `Docker-Content-Digest`. Клиент проверяет digest перед
сохранением: если содержимое подделано — оно не совпадёт с requested URL, и pull отклонится.

## Слоистая файловая система: OverlayFS

Контейнерный образ из 8 слоёв и весом 1 GB запускается за миллисекунды, потому что слои **не копируются** — они
монтируются стопкой через **OverlayFS** (или, исторически, AUFS, btrfs, devicemapper, ZFS).

OverlayFS — это union mount: несколько read-only нижних директорий (`lowerdir`) объединяются с одной
read-write верхней (`upperdir`) в логическую файловую систему `merged`. Запись идёт только в upperdir; для
изменения файла из lowerdir он сначала **копируется наверх** (copy-up), и дальше работа идёт с копией.

```
┌─────────────────────────────────────────────────────────────────────┐
│                  OverlayFS в контейнере                             │
│                                                                     │
│       merged/                                                       │
│      (то, что видит контейнер: /, /etc, /usr, /app, ...)            │
│            ▲                                                        │
│            │  union view                                            │
│            │                                                        │
│      ┌─────┴──────┐                                                 │
│      │            │                                                 │
│   upperdir       lowerdir (стопка, нижний первым)                   │
│   (rw)                                                              │
│  ┌──────────┐   ┌──────────┐  ┌──────────┐  ┌──────────┐            │
│  │ writes   │   │ layer 3  │  │ layer 2  │  │ layer 1  │  ← base    │
│  │ от       │   │ COPY     │  │ COPY app │  │ apt inst │            │
│  │ контей-  │   │ config   │  │          │  │          │            │
│  │ нера     │   │ (image)  │  │ (image)  │  │ (image)  │            │
│  └──────────┘   └──────────┘  └──────────┘  └──────────┘            │
│                                                                     │
│   + workdir (служебный, для атомарности rename внутри overlay)      │
└─────────────────────────────────────────────────────────────────────┘
```

### Copy-up

```
До изменения файла /etc/nginx.conf:
  merged:    /etc/nginx.conf  ──▶  читается из layer 1 (lower)
  upper:     —

Контейнер делает: echo "new" >> /etc/nginx.conf
  kernel: open(/etc/nginx.conf, O_RDWR)
    1. lookup в upper: нет
    2. lookup в lower: layer 1, файл найден
    3. copy-up: cp lower/etc/nginx.conf upper/etc/nginx.conf
    4. open в upper

После:
  merged:    /etc/nginx.conf  ──▶  читается из upper
  upper:     /etc/nginx.conf  (модифицированный)
```

Copy-up — это **разовая** операция: после неё файл живёт в upperdir и больше не трогает lower. Для маленьких
файлов цена незаметна, для крупных (видео-файл 4 GB, который контейнер только дописывает) — заметна. Решения:
если приложение пишет в большие файлы — монтировать volume в обход overlay; если только читает — overlay не
делает копию вовсе.

### Whiteout

Удаление файла из lower-слоя не может «стереть» сам lower (он read-only). Вместо этого в upperdir создаётся
специальный файл-маркер. На overlayfs это **character device 0:0** с тем же именем; в tar OCI-слое — файл с
префиксом `.wh.` (например, `.wh.config.ini` означает «config.ini удалён»). При union view kernel пропускает
файл с whiteout-маркером, как будто его нет.

```bash
# Посмотреть стопку слоёв конкретного контейнера
docker inspect --format '{{.GraphDriver.Data}}' my-container
# Driver: overlay2
# LowerDir: /var/lib/docker/overlay2/.../diff:/var/lib/docker/overlay2/.../diff:...
# UpperDir: /var/lib/docker/overlay2/<id>/diff
# WorkDir:  /var/lib/docker/overlay2/<id>/work
# MergedDir: /var/lib/docker/overlay2/<id>/merged
```

### Что значит «каждый RUN — новый слой»

```dockerfile
FROM ubuntu:22.04                # layer 0 (база)
RUN apt-get update               # layer 1: + индексы apt
RUN apt-get install -y curl      # layer 2: + бинари + библиотеки
COPY app/ /app                   # layer 3: + ваш код
CMD ["/app/start.sh"]            # config-only, не layer
```

Каждая `RUN`/`COPY`/`ADD` — новый tar-diff поверх предыдущего. Это даёт **кэширование** при пересборке (Docker
сравнивает hash инструкции — если не менялась, переиспользует слой) и **расшаривание** между образами
(`ubuntu:22.04` хранится один раз для всех образов на его основе).

Антипаттерн: `RUN apt-get install x && rm -rf /var/cache/apt/archives/*` в **одной** инструкции — потому что
если разделить на два RUN, кэш сохранится в первом слое, и второй слой добавит whiteout — суммарно занятого
места столько же, как до удаления.

## Слой runtime: runc, crun, youki

Когда containerd говорит «запусти контейнер из этого bundle» — он зовёт **low-level container runtime**. Его
задача: прочитать `config.json`, разложить пути, создать namespaces, применить cgroups, дропнуть capabilities,
загрузить seccomp-фильтр, сделать pivot_root и exec'нуть entrypoint. После запуска runtime обычно завершается —
контейнер живёт сам по себе.

| Runtime    | Язык     | Размер  | Особенности                                            |
|------------|----------|---------|--------------------------------------------------------|
| **runc**   | Go       | ~10 MB  | reference impl OCI, поддерживается opencontainers      |
| **crun**   | C        | ~500 KB | в 50× быстрее старта, меньше памяти, дефолт в Podman   |
| **youki**  | Rust     | ~6 MB   | memory-safe, экспериментальный, активная разработка    |
| **gVisor** | Go       | ~30 MB  | userspace kernel (sentry), перехватывает все syscall'ы |
| **Kata**   | Go + VM  | (+VM)   | каждый контейнер = микро-VM, KVM-изоляция              |

runc исторически появился из выделения runtime-кода из dockerd в 2015 году — это и был первый OCI-compliant
runtime. crun написан с целью «то же самое, но без Go-runtime'а»: на крупных кластерах разница в скорости старта
и расходе памяти на тысячи контейнеров заметна. youki — попытка переписать runc на Rust с акцентом на
memory safety и удобство расширения.

gVisor и Kata стоят особняком: они называют себя «runtimes», но изоляция у них другая. gVisor запускает
гостевые процессы под собственным userspace-«ядром» (Sentry), все syscall'ы гостя ловятся через seccomp+ptrace
и обрабатываются в userspace — это защищает host-kernel от 0-day. Kata оборачивает каждый контейнер в полноценную
KVM-VM с минимальным гостевым ядром — изоляция как у VM, API как у контейнера.

## Слой management: containerd, dockerd

Low-level runtime запускает один контейнер и забывает про него. Чтобы скачать образ, распаковать слои, отслеживать
жизнь сотни контейнеров, обрабатывать `docker exec`, прокидывать порты — нужен **container manager**.

```
┌──────────────────────────────────────────────────────────────────────┐
│                  layered architecture (Linux/Docker)                 │
│                                                                      │
│   ┌──────────────────┐                                               │
│   │   docker CLI     │   client (gRPC/HTTP к dockerd)                │
│   └────────┬─────────┘                                               │
│            │ /var/run/docker.sock                                    │
│   ┌────────▼─────────┐                                               │
│   │     dockerd      │   high-level: images, volumes, networks,      │
│   │                  │   build (BuildKit), swarm, plugins            │
│   └────────┬─────────┘                                               │
│            │ gRPC                                                    │
│   ┌────────▼─────────┐                                               │
│   │   containerd     │   image store, snapshotter (overlay),         │
│   │                  │   task lifecycle, CRI для Kubernetes          │
│   └────────┬─────────┘                                               │
│            │ shim API                                                │
│   ┌────────▼─────────┐                                               │
│   │  containerd-shim │   ОДИН на каждый контейнер; родитель init     │
│   │  (один на        │   процесса контейнера; держит stdio, reap'ит  │
│   │   контейнер)     │   зомби; переживает рестарт containerd        │
│   └────────┬─────────┘                                               │
│            │ exec                                                    │
│   ┌────────▼─────────┐                                               │
│   │   runc / crun    │   OCI runtime: clone+ns+cgrp+seccomp+exec,    │
│   │                  │   сразу после exec — exit (контейнер живёт)   │
│   └────────┬─────────┘                                               │
│            │                                                         │
│   ┌────────▼─────────┐                                               │
│   │ контейнерный     │   ваш nginx / postgres / app                  │
│   │ процесс (PID 1   │                                               │
│   │ в своём ns)      │                                               │
│   └──────────────────┘                                               │
└──────────────────────────────────────────────────────────────────────┘
```

### Зачем shim

Containerd-shim существует, чтобы **разделить жизненный цикл контейнера и containerd**. Если бы containerd был
прямым родителем процесса контейнера, рестарт containerd убил бы все контейнеры (или превратил их в orphan'ов,
которых reparent на PID 1). Вместо этого:

1. containerd просит runc создать контейнер.
2. runc форкается, в ребёнке делает setup ns/cgroup и exec entrypoint, выходит.
3. **Перед exec'ом** запускается shim как промежуточный родитель: shim делает `setsid` и становится отдельной
   process group.
4. Когда containerd убивают и рестартуют — shim'ы продолжают работать, держат stdio контейнеров, и containerd
   при старте пере-подключается к ним.

```
до shim (упрощённо):                    с shim:

  systemd                                 systemd
    └── containerd                          ├── containerd  (можно рестартовать)
          └── nginx (контейнер)             └── shim-abc    (PID 1 для nginx)
                                                  └── nginx (контейнер)

  рестарт containerd:                     рестарт containerd:
  nginx становится orphan'ом              nginx ничего не замечает,
  или умирает                             shim переживает рестарт
```

В Kubernetes из всей этой цепочки dockerd обычно убран: kubelet общается с containerd напрямую через
**CRI** (Container Runtime Interface). dockerd-shim переименован в containerd-shim-runc-v2, но суть та же.

## Что делает `docker run` пошагово

```
$ docker run -d --name web -p 8080:80 nginx:alpine

┌────────────────────────────────────────────────────────────────────────┐
│                       lifecycle docker run                             │
└────────────────────────────────────────────────────────────────────────┘

  Шаг 1. dockerd принимает HTTP-запрос от docker CLI.
         Проверяет, есть ли образ nginx:alpine в локальном content store.

  Шаг 2. Если нет — pull:
         containerd → реестру (distribution-spec):
           manifest → config → blobs (слои параллельно)
         сохраняет в /var/lib/containerd/io.containerd.content.v1.content/

  Шаг 3. Snapshotter (overlay) распаковывает слои:
         для каждого слоя — отдельная директория в
         /var/lib/containerd/io.containerd.snapshotter.v1.overlayfs/snapshots/<id>/fs/

         lowerdir = layer_0/fs:layer_1/fs:layer_2/fs

  Шаг 4. Готовится writable snapshot:
         mkdir snapshots/<container-id>/{fs,work}
         upperdir = <container-id>/fs
         workdir  = <container-id>/work

  Шаг 5. Mount overlay в bundle:
         mkdir /run/containerd/io.containerd.runtime.v2.task/<ns>/<id>/rootfs
         mount -t overlay overlay \
               -o lowerdir=L1:L2:L3,upperdir=U,workdir=W \
               .../rootfs

  Шаг 6. Готовится config.json (runtime-spec):
         linux.namespaces = [pid, net, ipc, uts, mount, cgroup]
         linux.resources  = (cpu, memory limits)
         process.args     = ["nginx", "-g", "daemon off;"]
         + mounts /proc, /sys, /dev/pts
         + seccomp profile (default Docker policy)
         + capabilities (default whitelist: CAP_NET_BIND_SERVICE, ...)

  Шаг 7. containerd зовёт shim, shim зовёт runc create:
         в runc:
           a) clone(CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET |
                    CLONE_NEWUTS | CLONE_NEWIPC [| CLONE_NEWUSER])
           b) (если user ns) write /proc/self/{uid,gid}_map
           c) sethostname()
           d) mount /proc, /sys, /dev, /tmp в новом mount ns
           e) pivot_root(rootfs)
           f) apply rlimits, set no_new_privs, drop capabilities
           g) install seccomp filter (после drop'а, иначе фильтр не загрузится)
           h) execve(entrypoint)

  Шаг 8. Параллельно cgroup-manager помещает PID контейнера в
         /sys/fs/cgroup/system.slice/docker-<id>.scope/
         с лимитами cpu.max, memory.max и т.д.

  Шаг 9. Networking:
         dockerd создаёт veth pair (vethXXX ↔ eth0-в-контейнере),
         host-конец подключает к bridge docker0,
         внутри контейнера: ip addr add 172.17.0.2/16 dev eth0
         в iptables: -t nat -A PREROUTING -p tcp --dport 8080
                     -j DNAT --to-destination 172.17.0.2:80

  Шаг 10. dockerd возвращает container ID клиенту.
          Контейнер работает. Shim — его родитель, держит stdio.
```

Время от docker run до старта entrypoint — обычно 50–200 мс на тёплом образе. Из них на runc приходится ~10 мс,
остальное — overhead containerd, snapshotter, networking.

## Networking: bridge mode

По умолчанию Docker создаёт виртуальный **bridge** `docker0` и подключает каждый контейнер к нему через свою
**veth pair**. Это даёт изолированную подсеть (например, 172.17.0.0/16), внутренний DNS между контейнерами и NAT
для исходящего трафика.

```
┌───────────────────────────────────────────────────────────────────────┐
│                      host (root netns)                                │
│                                                                       │
│            ┌──────────────────────────────────────┐                   │
│            │  bridge docker0  (172.17.0.1/16)     │                   │
│            │  ┌─────────┬─────────┬─────────┐     │                   │
│            │  │ vethA   │ vethB   │ vethC   │     │                   │
│            │  └────╥────┴────╥────┴────╥────┘     │                   │
│            └───────╫─────────╫─────────╫──────────┘                   │
│                    ║         ║         ║                              │
│         ┌──────────╫─────────╫─────────╫──────┐                       │
│         │ iptables ║         ║         ║      │   (FORWARD chain      │
│         │ -A PREROUTING -p tcp --dport 8080   │    проверяет, что     │
│         │    -j DNAT --to 172.17.0.2:80       │    bridge → eth0      │
│         │ -A POSTROUTING -s 172.17.0.0/16     │    разрешён)          │
│         │    -j MASQUERADE                    │                       │
│         └──────────╫─────────╫─────────╫──────┘                       │
│                    ║         ║         ║                              │
│           ┌────────╨──┐ ┌────╨───┐ ┌───╨───┐    eth0 ──▶ интернет     │
│           │ web (.0.2)│ │db(.0.3)│ │app(.4)│                          │
│           │ eth0 = veth (peer-конец, в контейнерном netns)            │
│           └───────────┘ └────────┘ └───────┘                          │
└───────────────────────────────────────────────────────────────────────┘
```

### Что делает Docker под капотом

```bash
# Создание bridge при старте dockerd (один раз)
ip link add docker0 type bridge
ip addr add 172.17.0.1/16 dev docker0
ip link set docker0 up
iptables -t nat -A POSTROUTING -s 172.17.0.0/16 ! -o docker0 -j MASQUERADE

# Для каждого контейнера:
ip link add vethA type veth peer name vethB
ip link set vethA master docker0
ip link set vethA up
ip link set vethB netns <container-pid>
nsenter -t <container-pid> -n ip link set vethB name eth0
nsenter -t <container-pid> -n ip addr add 172.17.0.2/16 dev eth0
nsenter -t <container-pid> -n ip link set eth0 up
nsenter -t <container-pid> -n ip route add default via 172.17.0.1

# Для опубликованного порта (-p 8080:80):
iptables -t nat -A PREROUTING -p tcp --dport 8080 \
         -j DNAT --to-destination 172.17.0.2:80
iptables -t nat -A OUTPUT     -p tcp --dport 8080 \
         -d 127.0.0.1 -j DNAT --to-destination 172.17.0.2:80
iptables -A FORWARD -d 172.17.0.2 -p tcp --dport 80 -j ACCEPT
```

Параллельно запускается **docker-proxy** — userspace TCP-proxy, который слушает 0.0.0.0:8080 и форвардит на
172.17.0.2:80. Зачем — если iptables и так делает DNAT? Затем, что DNAT не работает для loopback (`localhost:8080`
не транслируется через PREROUTING без специального правила в OUTPUT). docker-proxy покрывает этот случай. Многие
production-инсталляции отключают его (`--userland-proxy=false`) ради экономии памяти — тогда нужно настраивать
iptables OUTPUT-правила вручную.

### Другие сетевые режимы

| Режим            | Что делает                                                                   |
|------------------|------------------------------------------------------------------------------|
| `bridge`         | дефолт; контейнер в своём netns, подключён к docker0 через veth              |
| `host`           | контейнер в host'овском netns; видит все интерфейсы хоста, нет NAT, нет veth |
| `none`           | свой netns без интерфейсов (даже lo down); пишите сеть руками или не пишите  |
| `container:<id>` | подключиться к netns другого контейнера (sidecar-паттерн в k8s)              |
| `macvlan`        | контейнер получает собственный MAC-адрес на физическом интерфейсе            |
| `ipvlan`         | то же, но общий MAC, разные IP                                               |
| custom           | плагины CNI: Calico, Cilium, Flannel, Weave                                  |

В Kubernetes стандартный bridge не используется — там через **CNI plugin** настраивается overlay-сеть между нодами
(VXLAN, WireGuard, eBPF tunnel), которая даёт каждому Pod уникальный IP во всём кластере.

## Rootless контейнеры

Классический Docker требует daemon, работающий от root: создание veth, mount overlay, namespaces, cgroup-v1
manipulations — всё требует CAP_SYS_ADMIN. **Rootless** контейнеры (Podman, rootless Docker, Buildah) обходятся
без root через комбинацию user namespace + uidmap + slirp4netns.

```
┌──────────────────────────────────────────────────────────────────────┐
│                       rootless container                             │
│                                                                      │
│  user endor (UID 1000) на host                                       │
│      │                                                               │
│      │  podman run alpine sh                                         │
│      ▼                                                               │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │  user namespace (создан без root через unshare(CLONE_NEWUSER)) │  │
│  │                                                                │  │
│  │  uid_map: 0 1000 1                  ← root в ns = endor        │  │
│  │           1 100000 65536            ← из /etc/subuid           │  │
│  │                                                                │  │
│  │  внутри ns:                                                    │  │
│  │    UID 0 = root                                                │  │
│  │    может делать unshare(CLONE_NEWNET, CLONE_NEWNS, ...)        │  │
│  │    может mount tmpfs, overlay, pivot_root                      │  │
│  │    но НЕ может реально открыть raw socket на host,             │  │
│  │      привязаться к порту <1024,                                │  │
│  │      создать device-node, ...                                  │  │
│  │                                                                │  │
│  │  network: slirp4netns или pasta                                │  │
│  │    userspace TCP/IP-стек, читает пакеты из tap-устройства      │  │
│  │    внутри ns, отправляет наружу как обычные сокеты от endor    │  │
│  │                                                                │  │
│  │  storage: ~/.local/share/containers/storage                    │  │
│  │    overlayfs работает БЕЗ root, но требует kernel >= 5.11      │  │
│  │    или fuse-overlayfs как fallback                             │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

### Subordinate UIDs

`/etc/subuid` и `/etc/subgid` распределяют каждому пользователю диапазон UID, которые он может маппировать
**внутри своего user ns**:

```
$ cat /etc/subuid
endor:100000:65536
alice:165536:65536
```

Это означает: пользователю endor (UID 1000) разрешено в его user namespace маппировать «снаружние» UID
100000–165535 на «внутренние» 1–65536. Запись `0 1000 1` в `uid_map` маппирует root внутри ns на самого endor
снаружи. В сумме это даёт ему 65537 различных UID для процессов и файлов контейнера.

### Сетевые ограничения

Без CAP_NET_ADMIN на host'е rootless не может создать veth/bridge. Решение — userspace TCP/IP stack:

- **slirp4netns** — программа на стороне host'а, которая работает как «модем» между tap-интерфейсом
  внутри netns контейнера и обычными сокетами от имени UID 1000 на host'е. Просто, но overhead 30–50% на throughput.
- **pasta** (passt) — современный быстрый аналог; использует zero-copy через io_uring, throughput близок к bridge.

Привязка к портам <1024 (privileged ports) внутри rootless контейнера видна как «root делает bind», но снаружи
этот процесс — обычный UID 1000, которому ядро не даст открыть, например, порт 80 на 0.0.0.0. Решения: `sysctl
net.ipv4.ip_unprivileged_port_start=80` (понизить порог), либо использовать `-p 8080:80` (внутренний 80 → внешний
8080, и 8080 уже unprivileged).

### Когда rootless не подходит

- **CRI-O / kubelet** на ноде Kubernetes — нужны root-привилегии для управления физическими интерфейсами и
  большими mount-операциями.
- **PID limits**: pids cgroup делегируется в user.slice пользователя, но общий лимит на хосте всё ещё `pid_max`.
- **Storage drivers**: btrfs/devicemapper rootless не работают, только overlay (с native >= 5.11) или fuse-overlay.
- **systemd внутри контейнера** работает плохо без полного user@1000.service setup'а.

## Подводные камни

**OverlayFS и inotify через слои.** Если файл лежит в lower (read-only), inotify-watcher на нём не получает события
о copy-up как «modify» — он привязан к inode, а после copy-up это другой inode. Многие watch-приложения
(file-syncing, hot-reload в Node.js) ломаются на overlay непредсказуемо.

**OverlayFS и fsync.** `fsync` в overlay-файле, ещё не прошедшем copy-up, сначала делает copy-up, потом fsync.
Это превращает быстрый «прочитать файл и закрыть» в «скопировать 100 MB и тогда fsync». Базы данных в качестве
storage внутри overlay — антипаттерн; volume в обход overlay — правильный путь.

**iptables и FORWARD chain.** Docker по умолчанию ставит `net.ipv4.ip_forward=1` и добавляет правила в FORWARD.
Если на host'е работает firewall, который сбрасывает FORWARD-цепь (например, ufw), он сломает Docker-сеть.
Решение: либо не использовать ufw/firewalld с Docker, либо настраивать их совместимо.

**Image bloat от плохого Dockerfile.** Каждый RUN — новый слой. `RUN wget X && rm X` — два слоя: первый с
файлом, второй с whiteout. Файл всё равно занимает место в первом слое. Правильно — одной командой:
`RUN wget X && process X && rm X` (всё в одном слое).

**`docker exec` ≠ ssh.** `docker exec` запускает новый процесс в namespaces работающего контейнера через `setns`,
но **наследует не все** state'ы. Например, environment variables берутся не из контейнера, а из текущего shell'а
(если `-e` явно не передано). nsenter под капотом и нюансы те же.

**Layer caching и зависимости.** `COPY package.json . && RUN npm install` сохраняет npm-кэш между билдами, но
только если `package.json` не менялся (hash инструкции зависит от содержимого). `COPY . . && RUN npm install`
ломает кэш на любом изменении кода — `npm install` будет каждый раз заново.

**OCI image manifest list (multi-arch).** На `docker pull alpine:latest` сначала приходит **index** со списком
manifest'ов для разных архитектур; клиент сам выбирает свой. Если в кастомном реестре сломалась индексация
архитектур, можно получить ARM-образ на x86 host'е с непонятной ошибкой `exec format error`.

## Инструменты

```bash
# Низкоуровневые
runc list                          # запущенные OCI-контейнеры
runc spec                          # сгенерировать config.json в bundle/
ctr -n moby c ls                   # containerd CLI (namespace moby = dockerd)
ctr -n moby snapshots ls           # snapshot'ы (overlay-стопки)
nerdctl run nginx                  # CLI к containerd, как docker

# Инспекция образа
skopeo inspect docker://alpine:latest        # manifest без скачивания
skopeo copy docker://alpine:latest oci:./out # скачать в OCI-формат
dive my-image:tag                            # просмотр слоёв и diff'ов

# Сборка без daemon
buildah bud -t my-image .
img build -t my-image .
buildkit (как daemon или daemonless)

# Низкоуровневая отладка контейнера
nsenter -t $(docker inspect -f '{{.State.Pid}}' web) -m -u -p -n /bin/sh
crictl ps                          # CRI клиент для k8s nodes

# Просмотр сетевой настройки
docker network ls
docker network inspect bridge
ip -d link show docker0
iptables -t nat -L -n -v
```

## Связанные темы

- [Linux namespaces](namespaces.md) — фундамент: изоляция mount/pid/net/uts/ipc/user/cgroup/time
- [cgroups: углублённо](cgroups.md) — ограничение ресурсов контейнера; v2 hierarchy, memory/io/pids, PSI
- [seccomp](seccomp.md) — фильтр syscall'ов; накладывается в самом конце runc setup перед execve
- [eBPF](../ebpf/foundations.md) — современный networking в Kubernetes (Cilium), наблюдение контейнеров
- [systemd](../boot_and_init/systemd.md) — systemd-nspawn как минимальный container runtime, scope для docker

## Источники

- [OCI Image Specification](https://github.com/opencontainers/image-spec/blob/main/spec.md)
- [OCI Runtime Specification](https://github.com/opencontainers/runtime-spec/blob/main/runtime.md)
- [OCI Distribution Specification](https://github.com/opencontainers/distribution-spec/blob/main/spec.md)
- [runc source](https://github.com/opencontainers/runc) — `libcontainer/`, `init.go`
- [containerd architecture](https://github.com/containerd/containerd/blob/main/docs/historical/design/architecture.md)
- Liz Rice, «Containers from Scratch» — [запись на YouTube](https://www.youtube.com/watch?v=8fi7uSYlOdc)
- [overlayfs — kernel.org](https://www.kernel.org/doc/html/latest/filesystems/overlayfs.html)
- [Docker networking deep dive](https://docs.docker.com/network/) и `man iptables-extensions` (DNAT, MASQUERADE)
- [Rootless containers — rootlesscontaine.rs](https://rootlesscontaine.rs/)
