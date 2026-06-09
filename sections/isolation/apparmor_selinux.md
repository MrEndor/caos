# AppArmor и SELinux: Mandatory Access Control

Классическая модель доступа UNIX — DAC (Discretionary Access Control) — отдаёт решения на откуп владельцу
файла: владелец процесса с правами на файл может его прочитать, владелец файла может изменить permissions
кому угодно. У этой модели одна фундаментальная слабость: **скомпрометированный процесс с правами root
может всё**. Если nginx (запущенный как root для bind на 80-й порт) пробит уязвимостью в C-коде, exploit
получает доступ ко всей системе — потому что DAC root безусловен.

**MAC** (Mandatory Access Control) ставит над DAC второй слой решений, который описывает не пользователь,
а **system policy**. Даже root, даже владелец файла не могут разрешить больше, чем policy. Идея пришла из
оборонных систем середины XX века (Bell-LaPadula, 1973), в Linux — через **LSM** (Linux Security Module)
framework, в который встраиваются два основных MAC-модуля: **AppArmor** (path-based, простой) и
**SELinux** (label-based, мощный, сложный).

Применений много. Скомпрометированный daemon не выпустит exploit за пределы своей policy — это principle
of least privilege в виде кода. Контейнерные runtimes (Docker, podman, Kubernetes) автоматически генерируют
MAC-профили для каждого контейнера, чтобы пробив в одном контейнере не дал доступа к соседнему. Android
полностью построен на SELinux (с 4.3 в enforcing mode) — иначе экосистема приложений была бы не безопаснее
обычного desktop Linux.

## DAC vs MAC

```
              DAC (классический UNIX)                  MAC (AppArmor / SELinux)

   ┌────────────────────────────────┐         ┌────────────────────────────────┐
   │  process tries open(file)      │         │  process tries open(file)      │
   └───────────────┬────────────────┘         └───────────────┬────────────────┘
                   ▼                                          ▼
   ┌────────────────────────────────┐         ┌────────────────────────────────┐
   │  DAC check:                    │         │  DAC check  ✓ (как раньше)     │
   │  • uid match owner?            │         └───────────────┬────────────────┘
   │  • gid match group?            │                         ▼
   │  • mode bits allow?            │         ┌────────────────────────────────┐
   │  • capabilities (CAP_DAC_*)?   │         │  LSM hook → policy module      │
   └───────────────┬────────────────┘         │  • subject's profile/context   │
                   ▼                          │  • object's path/label         │
              allow / deny                    │  • requested permission        │
                                              └───────────────┬────────────────┘
                                                              ▼
                                                       allow / deny
                                              (даже root может получить deny)
```

Ключевое: **MAC проверяется после успешного DAC**. Если DAC отказал, MAC не вызывается. MAC только сужает.
Поэтому ослабление policy не открывает то, что закрыто DAC; усиление policy — закрывает то, что DAC открывал.

## LSM framework

LSM — общая infrastructure для security-модулей, добавлен в Linux 2.6.0 (2003). До неё SELinux существовал как
большой patchset, который никак не уживался с другими security-системами. LSM формализовал точки расширения:
~250 hooks по всему ядру, в которые модуль может подцепиться и вернуть verdict.

```
                     LSM hooks в kernel

  ┌─────────────────────────────────────────────────────────────────┐
  │                       userspace process                         │
  └──────────────────────────────┬──────────────────────────────────┘
                                 │  open("/etc/shadow", O_RDONLY)
   ══════════════════════════════│══════════════════════════════════ kernel
                                 ▼
                        ┌────────────────┐
                        │ syscall entry  │
                        └────────┬───────┘
                                 ▼
                        ┌────────────────┐
                        │  DAC check     │   ◀── проверка mode bits, capabilities
                        └────────┬───────┘
                                 ▼
                        ┌────────────────┐
                        │  LSM hook:     │   ◀── security_inode_permission()
                        │  inode_perm    │       вызывает зарегистрированные
                        └────────┬───────┘       модули по очереди
                                 │
             ┌──────────────────┼──────────────────┐
             ▼                  ▼                  ▼
        ┌──────────┐       ┌──────────┐       ┌──────────┐
        │ SELinux  │       │ AppArmor │       │  BPF LSM │
        │  module  │       │  module  │       │  (KRSI)  │
        └────┬─────┘       └────┬─────┘       └────┬─────┘
             │                  │                  │
             └──────────────────┼──────────────────┘
                                ▼
                         verdict (0 = allow,
                              -EACCES = deny)
                                │
                                ▼
                       continue / fail syscall
```

Исторически активным мог быть только **один major module** (SELinux **или** AppArmor, не оба одновременно).
С Linux 5.0 (2019) появился **LSM stacking** — несколько minor-модулей (Smack, Tomoyo, BPF LSM) могут работать
параллельно. Major-модули по-прежнему взаимоисключающие в большинстве дистрибутивов.

Какие модули активны:

```bash
cat /sys/kernel/security/lsm
# capability,lockdown,yama,apparmor,bpf
# или:
# capability,lockdown,yama,selinux,bpf
```

## AppArmor

AppArmor родился в 1998 году в SubDomain (Immunix), позже Novell приобрёл его и довёл до slot в kernel
(merged в 2.6.36, 2010). Сейчас по умолчанию в Ubuntu, openSUSE, Debian.

Основная идея — **path-based**: profile говорит «бинарь по такому пути может читать такой путь и писать туда»,
без каких-либо меток на файлах. Совместимость с обычной файловой системой полная — никакого extended attributes,
никакой relabel при копировании файлов.

### Profile

Profile — текстовый файл в `/etc/apparmor.d/<имя>`, обычно по convention `path.to.binary` с заменой `/` на `.`:

```
# /etc/apparmor.d/usr.sbin.nginx
#include <tunables/global>

/usr/sbin/nginx {
  #include <abstractions/base>
  #include <abstractions/nameservice>

  capability net_bind_service,
  capability setuid,
  capability setgid,

  network inet stream,
  network inet6 stream,

  /usr/sbin/nginx                mr,        # read+exec себя
  /etc/nginx/**                  r,         # рекурсивно r
  /var/log/nginx/*.log           w,         # запись логов
  /var/cache/nginx/**            rw,
  /var/www/**                    r,
  /run/nginx.pid                 rw,

  /proc/sys/kernel/random/uuid   r,

  deny /etc/shadow               r,         # явно запретить
  deny /home/**                  rw,
}
```

Синтаксис правил:

| Символ  | Значение                                                       |
|---------|----------------------------------------------------------------|
| `r`     | read                                                           |
| `w`     | write                                                          |
| `a`     | append (без truncate)                                          |
| `x`     | execute (требует subprofile, см. ниже)                         |
| `m`     | mmap PROT_EXEC                                                 |
| `k`     | lock                                                           |
| `l`     | link                                                           |
| `*`     | один компонент пути (не пересекает `/`)                        |
| `**`    | любой путь рекурсивно                                          |
| `{a,b}` | альтернатива                                                   |
| `[abc]` | класс символов                                                 |

При `exec()` нового бинаря важно, какой профиль будет применён — три режима:

- **`ix`** (inherit) — exec выполнен под текущим profile;
- **`px`** (profile) — exec'нутый бинарь должен иметь собственный profile, иначе fail;
- **`ux`** (unconfined) — exec выходит из-под профиля целиком (опасно).

### Modes

Profile может быть в одном из режимов:

| Mode       | Поведение                                                                 |
|------------|---------------------------------------------------------------------------|
| `enforce`  | нарушения блокируются (`-EACCES`) и логируются                            |
| `complain` | нарушения логируются, но разрешаются — режим обучения                     |
| `audit`    | разрешённые операции тоже логируются (отладка)                            |
| `disable`  | profile загружен, но не применяется                                       |

```bash
aa-status              # что загружено и в каком режиме
aa-enforce nginx       # перевести profile в enforce
aa-complain nginx      # в complain (для отладки/обучения)
aa-disable nginx       # выключить
```

### Learning mode

Главная фича AppArmor — генерация profile через **learning**:

```bash
aa-genprof /usr/sbin/nginx        # complain + парс логов
# (запустить нагрузочный сценарий nginx в другом терминале)
aa-logprof                         # интерактивно: для каждого denied
                                   # ask: allow / deny / glob / abstraction
```

Утилита показывает каждый denied access и предлагает добавить правило, использовать abstraction (готовый
набор правил для типичной задачи), или просто запретить. Profile получается специфичным для конкретной
инсталляции.

### Path-based: преимущества и слабости

```
                AppArmor path-based: один и тот же inode под разными именами

  /var/log/app/data.log  ──┐
                            ├──▶  один inode, разные пути
  /backup/symlink-to-log ──┘

  profile разрешает:  /var/log/app/data.log w
  через симлинк:      /backup/symlink-to-log w     ← может быть разрешено
                                                     если abstraction разрешает
```

Class бага: bind mount/symlink. AppArmor смотрит на путь, под которым файл открывают. Если файл доступен
по двум именам (один через bind-mount, другой через симлинк), правила должны учитывать оба. Confused-deputy
атаки на AppArmor исторически использовали это (CVE-2016-1585 и подобные).

**Защита от symlink** включается через `link` permission и `owner` ограничения, но это требует
аккуратности от автора profile.

## SELinux

SELinux разработан NSA на основе исследований Flask/Fluke в 1990-х, открыт в 2000 году, в mainline kernel —
2003 (через LSM framework, разработанный для него). По умолчанию в RHEL, Fedora, CentOS, AlmaLinux, RockyLinux,
Android.

Главная идея — **label-based**: каждый объект (файл, процесс, socket, IPC) и каждый субъект (процесс) имеет
**security context** — четырёхкомпонентный label. Решения принимаются по контекстам, а не по путям.

### Security context

Полный context: `user:role:type[:level]`:

```
   system_u:object_r:httpd_sys_content_t:s0
   ────┬─── ────┬──── ───────┬────────── ─┬─
       │        │            │            │
       │        │            │            └─ MLS level (опционально)
       │        │            │               s0 = default sensitivity
       │        │            │
       │        │            └─ TYPE — основа решений
       │        │
       │        └─ ROLE — допустимый набор типов для пользователя
       │
       └─ SELinux USER — отдельно от Unix UID
          (типичные: system_u для system, unconfined_u для login,
          user_u/staff_u для restricted accounts)
```

Type — главное. Имена типов по convention заканчиваются на `_t`. Файлы конфигурации nginx имеют тип
`httpd_config_t`, контент — `httpd_sys_content_t`, сам процесс nginx — `httpd_t`. Решения принимаются по
паре `(source_type, target_type)`.

```bash
# context процесса
ps -eZ | grep nginx
# system_u:system_r:httpd_t:s0   1234 ?   00:00:01 nginx

# context файла
ls -Z /var/www/html/index.html
# system_u:object_r:httpd_sys_content_t:s0 index.html

# context socket'а
ss -Z
```

### Type Enforcement (TE)

Базовое правило policy:

```
allow source_type target_type:object_class { permissions };
```

Пример:

```
allow httpd_t httpd_sys_content_t:file { read getattr open };
allow httpd_t httpd_config_t:file       { read getattr open };
allow httpd_t http_port_t:tcp_socket    { name_bind };
```

Расшифровка: процесс с типом `httpd_t` может читать файл с типом `httpd_sys_content_t` (`read`/`open`/`getattr`),
читать файл с типом `httpd_config_t`, и биндить tcp_socket на порт с типом `http_port_t` (то есть 80, 443, etc.).
Любая операция, для которой нет `allow`, **запрещена по умолчанию** (default-deny).

```
                      Policy decision flow в SELinux

  syscall: nginx → open("/var/www/index.html")
                         │
                         ▼
              DAC check ✓
                         │
                         ▼
        SELinux hook: file_open
                         │
                         ▼
   ┌─────────────────────────────────────────┐
   │  source context = httpd_t               │
   │  target context = httpd_sys_content_t   │
   │  class          = file                  │
   │  permission     = open                  │
   └────────────────────┬────────────────────┘
                        ▼
        ╱─────────────────────────────╲
       │   AVC (Access Vector Cache)   │ ◀── hash-кэш recent decisions
        ╲─────────────────────────────╱
                        │
              hit ──────┴────── miss
               │                │
               ▼                ▼
            allow         consult policy
                                │
                                ▼
                    allow rule exists? ─── yes → allow + cache
                                │
                                no
                                │
                                ▼
                          deny + AVC log
```

### RBAC, MLS, MCS

Полная модель SELinux — **MAC + Type Enforcement + RBAC + MLS + MCS**:

| Уровень | Что добавляет                                                                                  |
|---------|------------------------------------------------------------------------------------------------|
| TE      | базовое решение по type (`allow source target:class perm`)                                     |
| RBAC    | роли (`role`) ограничивают, какие types может assume SELinux user                              |
| MLS     | многоуровневая секретность (Bell-LaPadula): `s0` … `s15`, no read-up / no write-down           |
| MCS     | категории (`c0` … `c1023`) для изоляции инстансов одного приложения (например, контейнеров)    |

В обычном Linux MLS не используется (это для compartmented military systems). MCS — основной механизм
изоляции контейнеров: каждому контейнеру даётся уникальная пара категорий, и файлы помечаются так, что
один контейнер не видит файлы другого.

### Хранение labels

Labels файлов хранятся в xattr `security.selinux`:

```bash
getfattr -n security.selinux /var/www/html/index.html
# security.selinux="system_u:object_r:httpd_sys_content_t:s0"
```

Файлы без xattr получают default по policy (`file_contexts`). При создании файла type наследуется от parent
directory (плюс правила type_transition). При копировании через `cp` без `--preserve=context` файл получает
тип, как будто создан в destination — поэтому копирование `index.html` из home директории в `/var/www/html/`
даёт ему правильный `httpd_sys_content_t`, а перемещение через `mv` — оставляет старый тип `user_home_t`, и
nginx его не сможет читать.

### Modes

```bash
getenforce
# Enforcing | Permissive | Disabled

setenforce 0          # переключить в Permissive (временно, до reboot)
setenforce 1          # в Enforcing
```

| Mode         | Поведение                                                  |
|--------------|------------------------------------------------------------|
| `Enforcing`  | запреты применяются                                        |
| `Permissive` | запреты логируются (AVC denied), но операции разрешены     |
| `Disabled`   | SELinux выключен полностью (не загружен policy)            |

`Disabled` требует reboot и считается deprecated (с Linux 6.4 настоятельно не рекомендуется). Для отладки —
`Permissive`.

### Booleans

Boolean — runtime toggle для policy без перекомпиляции. Их сотни:

```bash
getsebool -a | grep httpd | head
# httpd_can_network_connect           --> off
# httpd_can_network_connect_db        --> off
# httpd_can_sendmail                  --> off
# httpd_enable_homedirs               --> off

setsebool -P httpd_can_network_connect on    # -P = persistent
```

Каждый boolean соответствует группе rules внутри policy, которые включаются/выключаются. Это самый частый
способ adjust policy для конкретной инсталляции — без написания собственных модулей.

### Reference Policy

Reference Policy — modular policy, разрабатываемая Tresys/Red Hat, ~10000+ rules по умолчанию. Загружается
по модулям из `/etc/selinux/<policy>/modules/`. Существует исторически и Strict Policy (deprecated), и
**MLS Policy** (для систем с многоуровневой секретностью).

Большинство дистрибутивов используют **targeted policy** — confined только daemons (httpd, sshd, postgresql,
…), interactive user sessions работают как `unconfined_t` (минимальные ограничения). На production-серверах
полноценная защита, на desktop — минимум помех.

### Container support: svirt и Kubernetes

Docker (с интеграцией SELinux), podman, Kubernetes (через CRI) используют MCS для изоляции контейнеров:

```bash
# два контейнера получают разные категории
podman run --rm alpine cat /proc/self/attr/current
# system_u:system_r:container_t:s0:c123,c456

podman run --rm alpine cat /proc/self/attr/current
# system_u:system_r:container_t:s0:c789,c012
```

Файлы внутри volume помечаются категорией контейнера, что делает их недоступными другим контейнерам даже
при общем underlying mount.

## Сравнение AppArmor vs SELinux

| Свойство                | AppArmor                          | SELinux                                       |
|-------------------------|-----------------------------------|-----------------------------------------------|
| Подход                  | path-based                        | label-based (Type Enforcement)                |
| Гранулярность           | per-binary path                   | per-domain (несколько binaries → один type)   |
| Сложность policy        | простая, читаемая                 | сложная, ~10000+ rules в reference policy     |
| Кривая обучения         | несколько часов                   | дни-недели                                    |
| Generation tools        | `aa-genprof`, `aa-logprof`        | `audit2allow`                                 |
| Bind-mount / symlink    | уязвимо (path меняется)           | безопасно (label следует за inode)            |
| Хранение labels         | не нужно                          | `security.selinux` xattr на каждом inode      |
| MLS / MCS               | нет                               | есть                                          |
| Container integration   | docker-default profile            | MCS на каждый контейнер (svirt)               |
| Default в               | Ubuntu, openSUSE, Debian          | RHEL, Fedora, CentOS, Android                 |
| Stacked LSM (5.0+)      | да (stacking minor)               | да (stacking minor)                           |
| Когда выбрать           | desktop/embedded, простота важнее | server/multi-tenant, нужна строгая изоляция   |

## Troubleshooting SELinux

Большая часть проблем — «процесс не может прочитать/записать файл, хотя DAC permissions правильные».
Workflow:

```bash
# 1. Проверить, действительно ли SELinux виноват
getenforce                       # должно быть Enforcing
setenforce 0                     # временно в Permissive
# повторить операцию: работает? → виноват SELinux
setenforce 1                     # обратно в Enforcing

# 2. Посмотреть последние denied
ausearch -m AVC -ts recent
# или (более user-friendly)
sealert -a /var/log/audit/audit.log

# 3. Понять, почему именно denied
# в выводе AVC будут source context, target context, class, permission

# 4. Если файл должен иметь другой type — поправить label
chcon -t httpd_sys_content_t /var/www/html/index.html  # одноразово
semanage fcontext -a -t httpd_sys_content_t '/var/www(/.*)?'  # в policy
restorecon -Rv /var/www                                # применить

# 5. Если type правильный, но действие действительно нужно — boolean
getsebool -a | grep ...
setsebool -P httpd_can_network_connect on

# 6. Если ничего из этого — сгенерировать собственный module
ausearch -m AVC -ts recent | audit2allow -M mymod
semodule -i mymod.pp
```

**`restorecon`** — самая частая команда: восстановить дефолтные labels по таблице `file_contexts`. После
любого `cp`/`mv`/`tar` файлов в системные директории первым делом — `restorecon -Rv <path>`.

## Современные тенденции

| Механизм      | Доступен с | Что нового                                                              |
|---------------|------------|-------------------------------------------------------------------------|
| **Landlock**  | 5.13 (2021)| unprivileged MAC — приложение само ограничивает свои дочерние процессы  |
| **BPF LSM**   | 5.7 (2020) | LSM hooks реализуются eBPF-программой (KRSI — Kernel Runtime Integrity) |
| **IPE**       | 6.12       | Integrity Policy Enforcement — verify-only policy для exec/load         |

**Landlock** интересен тем, что не требует root: процесс сам зовёт `landlock_create_ruleset()` и накладывает
ограничения на себя и потомков. Это меняет модель — не админ описывает policy для всей системы, а само
приложение sandbox'ит себя. Подходит для браузеров, document viewers, любых программ с untrusted input.

**BPF LSM** позволяет писать policy на C, компилировать в eBPF и attach'ить к LSM hooks без перезагрузки.
Tetragon (Cilium) использует это для runtime defense в Kubernetes — динамическая policy без AppArmor/SELinux.

## Tools cheat-sheet

```bash
# AppArmor
aa-status                          # текущее состояние профилей
aa-enforce /etc/apparmor.d/usr.sbin.nginx
aa-complain /etc/apparmor.d/usr.sbin.nginx
aa-genprof /path/to/binary         # learning mode + интерактивно
aa-logprof                         # додумать правила из логов
apparmor_parser -r /etc/apparmor.d/profile   # reload profile

# SELinux
getenforce / setenforce 0|1
ls -Z, ps -Z, ss -Z, id -Z         # показать contexts
chcon -t <type> <file>             # одноразово сменить type
semanage fcontext -a -t <type> '<pattern>'   # в policy
restorecon -Rv <path>              # применить дефолтные labels
getsebool -a / setsebool -P <bool> on
sestatus                           # статус, policy, mode
ausearch -m AVC -ts recent         # denied events
audit2allow -a -M <mod>            # сгенерировать module из denials
semodule -l                        # список загруженных модулей
semodule -i <mod.pp>               # загрузить module
```

## Связанные темы

- [seccomp](seccomp.md) — фильтрация syscall как комплементарный к MAC механизм
- [Linux namespaces](namespaces.md) — изоляция view, MAC дополняет её ограничением действий
- [Внутреннее устройство контейнеров](containers_internals.md) — как AppArmor/SELinux вписаны в Docker/podman
- [eBPF для безопасности: LSM, KRSI, Tetragon](../ebpf/security.md) — BPF LSM как динамическая альтернатива

## Источники

- [SELinux Project wiki](https://github.com/SELinuxProject/selinux/wiki)
- [AppArmor documentation](https://gitlab.com/apparmor/apparmor/-/wikis/Documentation)
- [SELinux Notebook (бесплатная книга)](https://github.com/SELinuxProject/selinux-notebook)
- [Red Hat: SELinux User's and Administrator's Guide](https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/using_selinux/)
- [Landlock: unprivileged access control](https://landlock.io/)
- [Linux Security Modules — LWN](https://lwn.net/Articles/606329/)
- `man 8 apparmor`, `man 8 selinux`, `man 5 selinux-config`
