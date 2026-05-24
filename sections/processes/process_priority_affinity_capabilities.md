# Приоритеты, CPU affinity и capabilities процесса

## Приоритет процесса

**Приоритет процесса** определяет, как часто планировщик ядра выделяет процессу процессорное время. В Linux существует
два уровня системы приоритетов.

### Nice-приоритет

Nice-приоритет — классическая UNIX-система приоритетов для обычных (non-realtime) процессов. Значение nice варьируется
от −20 до 19:

```
Планировщик Linux — иерархия приоритетов

  Realtime (RT) процессы           Обычные (CFS) процессы
  SCHED_FIFO / SCHED_RR            SCHED_OTHER / SCHED_BATCH

  rt_prio 99 (высший)
  ┌────────────────────┐
  │ RT процесс 1       │  ← вытесняет любой CFS-процесс
  │ SCHED_FIFO         │
  ├────────────────────┤
  │ RT процесс 2       │
  │ SCHED_RR           │
  ├────────────────────┤
  │ ...                │
  ├────────────────────┤
  │ RT процесс N       │
  │ rt_prio 1 (низший) │
  └────────────────────┘
          │ если нет RT-задач
          ▼
  nice -20 (высший, только root)
  ┌────────────────────┐
  │   proc A  nice=-20 │
  ├────────────────────┤
  │   proc B  nice=0   │  ← по умолчанию
  ├────────────────────┤
  │   proc C  nice=10  │
  ├────────────────────┤
  │   proc D  nice=19  │
  └────────────────────┘
  nice +19 (низший)
```

| Значение | Смысл                                                            |
|----------|------------------------------------------------------------------|
| −20      | Максимальный приоритет — процесс получает CPU в первую очередь   |
| 0        | Стандартный приоритет (по умолчанию)                             |
| 19       | Минимальный приоритет — процесс получает CPU в последнюю очередь |

Уменьшить nice-значение (повысить приоритет) может только root. Увеличить его (снизить приоритет) может любой
пользователь.

Узнать текущее nice-значение:

```bash
ps -o pid,nice,comm
```

Из кода:

```c
#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int nice_value = getpriority(PRIO_PROCESS, getpid());
    printf("Nice = %d\n", nice_value);
    return 0;
}
```

Изменить nice-приоритет:

```bash
nice -n 10 ./prog       # запустить программу с nice=10
renice -n 5 -p <pid>    # изменить приоритет уже работающего процесса
```

Из кода:

```c
#include <sys/resource.h>

// Установить nice=10 текущему процессу
setpriority(PRIO_PROCESS, 0, 10);
```

### Real-time приоритеты

Для задач с жёсткими требованиями по времени отклика Linux предоставляет real-time планировщики. RT-приоритеты принимают
значения от 1 до 99 и требуют привилегий (`CAP_SYS_NICE`):

- **SCHED_FIFO** — первый вошедший, первый вышел: процесс занимает CPU до тех пор, пока не заблокируется или не уступит
  его явно;
- **SCHED_RR** — аналогично FIFO, но с квантованием времени (round-robin) среди процессов с одинаковым приоритетом.

RT-процессы всегда вытесняют обычные (nice) процессы вне зависимости от их nice-значения.

## CPU Affinity

**CPU affinity** — это битовая маска, которая определяет, на каких процессорных ядрах разрешено выполняться данному
процессу. По умолчанию процесс может работать на любом доступном ядре.

Управление affinity полезно в нескольких сценариях:

- улучшение cache-локальности (процесс остаётся на одном ядре и его данные не вытесняются из L1/L2 кэша);
- привязка критичных по времени задач к изолированным ядрам;
- ручная балансировка нагрузки на многопроцессорных системах.

Узнать текущую affinity-маску:

```bash
taskset -p <pid>          # вывести affinity в виде шестнадцатеричной маски
ps -eo pid,psr,comm       # PSR — номер ядра, на котором процесс выполняется сейчас
```

Из кода:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    cpu_set_t mask;
    sched_getaffinity(getpid(), sizeof(mask), &mask);

    // Проверить, включено ли ядро 0 в маску
    if (CPU_ISSET(0, &mask)) {
        printf("Ядро 0 входит в affinity-маску\n");
    }
    return 0;
}
```

Установить affinity-маску:

```bash
taskset -cp 0,1 <pid>    # привязать работающий процесс к ядрам 0 и 1
taskset -c 0 ./prog      # запустить программу, привязав к ядру 0
```

Из кода:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>

int main() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);   // разрешить ядро 0
    CPU_SET(1, &mask);   // разрешить ядро 1

    sched_setaffinity(getpid(), sizeof(mask), &mask);
    return 0;
}
```

## Capabilities (привилегии процесса)

**Capabilities** — это механизм разделения привилегий суперпользователя на отдельные независимые права. Вместо бинарной
системы «root / не-root» ядро позволяет выдать процессу только те конкретные привилегии, которые ему действительно
необходимы.

Всего в Linux определено около 40 capabilities. Наиболее важные из них:

| Capability             | Назначение                                      |
|------------------------|-------------------------------------------------|
| `CAP_NET_BIND_SERVICE` | Привязка к портам с номером < 1024              |
| `CAP_NET_RAW`          | Создание raw-сокетов (нужно для ping)           |
| `CAP_NET_ADMIN`        | Широкие административные права над сетью        |
| `CAP_SYS_ADMIN`        | Общие административные привилегии (аналог root) |
| `CAP_SYS_TIME`         | Изменение системного времени                    |
| `CAP_SYS_CHROOT`       | Использование `chroot()`                        |
| `CAP_SETUID`           | Смена UID через `setuid()`                      |
| `CAP_DAC_OVERRIDE`     | Игнорирование стандартных прав доступа к файлам |
| `CAP_KILL`             | Отправка сигналов произвольным процессам        |

### Три набора capabilities

Каждый процесс имеет три отдельных набора (sets) capabilities:

```
┌─────────────────────────────────────────────┐
│              Permitted (P)                  │  ← «потолок»; нельзя
│  ┌───────────────────────────────────────┐  │    превысить
│  │           Effective (E)               │  │  ← ядро проверяет при
│  │  (то, что реально используется сейчас)│  │    каждой операции
│  └───────────────────────────────────────┘  │
│                                             │
│  ┌───────────────────────────────────────┐  │
│  │          Inheritable (I)              │  │  ← что переходит
│  │  (передаётся дочернему через exec)    │  │    в дочерний процесс
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘

  E ⊆ P  — effective всегда подмножество permitted
  P и I  — изменяются только с CAP_SETPCAP или через exec
```

**Permitted** — максимально возможный набор прав для данного процесса. Процесс не может получить capability, которой нет
в permitted. Это «потолок» привилегий.

**Effective** — набор прав, которые процесс активно использует прямо сейчас. Всегда является подмножеством permitted.
Процесс может динамически добавлять и убирать права в effective-наборе, но только в пределах permitted.

**Inheritable** — набор прав, которые могут передаваться дочернему процессу при вызове `exec()`. Используется совместно
с файловыми inheritable-флагами для управления наследованием привилегий.

| Характеристика | Permitted     | Effective              | Inheritable                   |
|----------------|---------------|------------------------|-------------------------------|
| Назначение     | Максимум прав | Текущие активные права | Права для передачи через exec |
| Изменяемость   | Сложно        | Легко                  | Сложно                        |
| Ограничение    | —             | ⊆ Permitted            | Независим                     |
| Проверка ядром | —             | При каждой операции    | При exec                      |

Посмотреть наборы capabilities через `/proc`:

```bash
cat /proc/self/status | grep Cap
# CapPrm: 000001ffffffffff   (permitted)
# CapEff: 000001ffffffffff   (effective)
# CapInh: 0000000000000000   (inheritable)
```

### Просмотр capabilities

```bash
getcap /usr/bin/ping      # capabilities исполняемого файла
getpcaps <pid>            # capabilities работающего процесса
```

Из кода с библиотекой `libcap`:

```c
#include <sys/capability.h>
#include <stdio.h>

int main() {
    cap_t caps = cap_get_proc();  // получить capabilities текущего процесса

    char *cap_text = cap_to_text(caps, NULL);
    printf("Capabilities: %s\n", cap_text);

    cap_flag_value_t val;
    cap_get_flag(caps, CAP_NET_RAW, CAP_PERMITTED, &val);
    printf("CAP_NET_RAW (permitted): %s\n", val ? "yes" : "no");

    cap_free(caps);
    return 0;
}
```

### Назначение capabilities файлу

Capabilities можно установить непосредственно на исполняемый файл с помощью `setcap`. Это позволяет запускать программу
без sudo, предоставив ей только нужные права:

```bash
# Разрешить серверу привязываться к портам < 1024
sudo setcap cap_net_bind_service=ep /usr/local/bin/myserver

# Разрешить ping создавать raw-сокеты
sudo setcap cap_net_raw=ep /usr/bin/ping

# Проверить:
getcap /usr/local/bin/myserver
# /usr/local/bin/myserver = cap_net_bind_service+ep
```

Суффиксы в строке capabilities:

- `e` — effective (активировать при запуске);
- `p` — permitted (разрешить);
- `i` — inheritable (передавать дочерним процессам через exec).

### Динамическое изменение capabilities из кода

```c
#include <sys/capability.h>
#include <stdio.h>

int main() {
    cap_t caps = cap_get_proc();

    // Добавить CAP_NET_RAW в effective набор
    cap_value_t cap_list[] = { CAP_NET_RAW };
    cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_SET);
    cap_set_proc(caps);

    // ... выполнить операцию, требующую CAP_NET_RAW ...

    // Убрать CAP_NET_RAW из effective (принцип наименьших привилегий)
    cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_CLEAR);
    cap_set_proc(caps);

    cap_free(caps);
    return 0;
}
```

Компиляция:

```bash
gcc prog.c -o prog -lcap
```

## cgroups v2 — CPU controller

`nice` и `sched_setaffinity` действуют на отдельные процессы. **cgroups (control groups)** работают совсем на другом
уровне: они объединяют процессы в иерархию и применяют лимиты к **группе целиком**. На современных дистрибутивах (Ubuntu
22.04+, Fedora 31+, RHEL 9) используется **cgroups v2** — единая унифицированная иерархия вместо разрозненных
контроллеров cgroups v1.

CPU controller cgroups v2 позволяет:

- задавать **относительный вес** группы при конкуренции за CPU (`cpu.weight`);
- ставить **жёсткий лимит** на процент CPU-времени (`cpu.max`);
- ограничивать набор разрешённых ядер (`cpuset.cpus`) — аналог affinity, но иерархический;
- собирать статистику использования (`cpu.stat`).

### Иерархия cgroups

Корень иерархии cgroups v2 смонтирован в `/sys/fs/cgroup`. Каждая поддиректория — это cgroup, наследующая ограничения
родителя:

```
/sys/fs/cgroup/                          ← root cgroup
├── cgroup.controllers                   ← какие контроллеры доступны (cpu memory io ...)
├── cgroup.subtree_control               ← какие контроллеры включены для детей
├── cpu.stat
│
├── system.slice/                        ← системные демоны (systemd)
│   ├── nginx.service/
│   │   ├── cgroup.procs                ← PIDs процессов в этой группе
│   │   ├── cpu.weight                  ← вес для CPU controller
│   │   └── cpu.max
│   └── postgresql.service/
│
├── user.slice/                          ← пользовательские сессии
│   └── user-1000.slice/
│       └── user@1000.service/
│           └── app.slice/
│               └── ...
│
└── mygroup/                             ← cgroup, созданная вручную
    ├── cgroup.procs                    ← echo $$ > сюда — добавить shell
    ├── cpu.weight                      ← 100 по умолчанию
    ├── cpu.max                         ← "max 100000" — без лимита
    └── cpuset.cpus                     ← пусто = наследуется
```

Принципы иерархии:

- Процесс всегда принадлежит ровно одной cgroup в v2 (в v1 мог принадлежать разным по контроллерам).
- Лимиты ребёнка не могут превышать лимиты родителя — все ограничения умножаются по цепочке.
- Чтобы контроллер был доступен в подгруппе, его нужно включить в родителе через `cgroup.subtree_control`.

### `cpu.weight` — относительные веса

`cpu.weight` определяет, какую долю CPU группа получает **при конкуренции** с другими группами. Значение в диапазоне
1..10000, по умолчанию 100. Это **soft-приоритет**: если других претендентов нет, группа использует всё, что захочет.

```
Два сервиса конкурируют за одно ядро

  group A: cpu.weight = 100       group B: cpu.weight = 300
        │                                │
        └── 25% CPU ────────────────────┴── 75% CPU
              (100 / (100+300))                (300 / (100+300))
```

Если в группе B нет активных задач, группа A получит 100% CPU. Это принципиальное отличие от `cpu.max`.

```bash
echo 200 > /sys/fs/cgroup/mygroup/cpu.weight   # в 2 раза приоритетнее дефолта
```

### `cpu.max` — жёсткий лимит

`cpu.max` устанавливает **hard cap**: максимальное процессорное время за окно. Формат — два числа через пробел:
`MAX_USAGE PERIOD` (оба в микросекундах).

```bash
# 50% одного ядра: 50000 мкс за каждые 100000 мкс
echo "50000 100000" > /sys/fs/cgroup/mygroup/cpu.max

# 2 ядра целиком: 200000 мкс за 100000 мкс
echo "200000 100000" > /sys/fs/cgroup/mygroup/cpu.max

# Снять лимит:
echo "max 100000" > /sys/fs/cgroup/mygroup/cpu.max
```

Когда квота исчерпана, группа замораживается до начала следующего периода. На графиках CPU это выглядит как пилообразный
паттерн.

| Параметр     | Тип            | Поведение при отсутствии конкуренции              |
|--------------|----------------|---------------------------------------------------|
| `cpu.weight` | soft, relative | группа берёт всё свободное CPU                    |
| `cpu.max`    | hard, absolute | группа упирается в потолок даже на пустой системе |

### `cpu.stat` — статистика

`cpu.stat` доступен для чтения и содержит накопленные счётчики:

```bash
cat /sys/fs/cgroup/mygroup/cpu.stat
# usage_usec 12345678              ← суммарное CPU-время в мкс
# user_usec 8000000                ← в user mode
# system_usec 4345678              ← в kernel mode
# nr_periods 1234                  ← сколько раз сработал период cpu.max
# nr_throttled 56                  ← сколько раз группу затормозили
# throttled_usec 789012            ← суммарно в throttle
```

Высокий `nr_throttled` или `throttled_usec` — признак, что `cpu.max` срезает рабочую нагрузку. В Kubernetes это типичная
причина внезапных p99-latency спайков у CPU-limited подов.

### `cpuset.cpus` — ограничение ядер

`cpuset` контроллер задаёт набор разрешённых CPU для всей cgroup. По смыслу — то же, что `taskset` на процесс, но
применяется к группе и наследуется:

```bash
echo "0-3" > /sys/fs/cgroup/mygroup/cpuset.cpus     # только ядра 0..3
echo "0,2,4" > /sys/fs/cgroup/mygroup/cpuset.cpus   # явный список
```

`cpuset.cpus.effective` показывает реально применённую маску с учётом ограничений родителя.

### Создание cgroup вручную

```bash
# 1. Создать cgroup — обычным mkdir
sudo mkdir /sys/fs/cgroup/mygroup

# 2. Включить нужные контроллеры в родителе
echo "+cpu +cpuset" | sudo tee /sys/fs/cgroup/cgroup.subtree_control

# 3. Настроить лимиты
echo 50 | sudo tee /sys/fs/cgroup/mygroup/cpu.weight
echo "50000 100000" | sudo tee /sys/fs/cgroup/mygroup/cpu.max

# 4. Добавить текущий shell (и все его дети — fork-нутые процессы)
echo $$ | sudo tee /sys/fs/cgroup/mygroup/cgroup.procs

# 5. Запустить нагрузку — она будет ограничена
yes > /dev/null &
top -p $!

# 6. Удалить cgroup (когда пуста)
sudo rmdir /sys/fs/cgroup/mygroup
```

Альтернатива — `systemd-run` создаёт transient cgroup автоматически:

```bash
sudo systemd-run --scope -p CPUQuota=50% -p CPUWeight=200 yes > /dev/null
```

### systemd-cgls и systemd-cgtop

systemd предоставляет удобные утилиты для просмотра иерархии:

```bash
systemd-cgls                    # дерево всех cgroups с процессами
systemd-cgls /system.slice      # только системные сервисы

systemd-cgtop                   # top-подобный интерфейс по cgroups
systemd-cgtop --cpu             # сортировка по CPU
systemd-cgtop -d 1              # обновление каждую секунду
```

`systemd-cgtop` показывает потребление CPU, memory, I/O сразу по группам — критически полезно при отладке «кто съел все
ресурсы» на multi-tenant машине.

### systemd-slices

systemd организует cgroups через **slices** — логические группировки сервисов. Slice — обычная cgroup с суффиксом
`.slice` в имени:

| Slice           | Содержимое                                            |
|-----------------|-------------------------------------------------------|
| `system.slice`  | системные демоны: nginx, postgres, sshd               |
| `user.slice`    | сессии пользователей (`user-1000.slice` для UID 1000) |
| `machine.slice` | контейнеры и VM (systemd-nspawn, libvirt)             |
| `init.scope`    | сам PID 1 (systemd)                                   |

Ограничения slice применяются ко всем сервисам внутри. Например, чтобы ограничить весь user.slice до 80% CPU:

```bash
sudo systemctl set-property user.slice CPUQuota=80%
# это запишет CPUQuota в drop-in unit-файл и применит через cgroup
```

Каждый `.service`-unit становится подгруппой соответствующего slice автоматически. В unit-файле можно указать:

```ini
[Service]
CPUWeight = 200
CPUQuota = 50%
AllowedCPUs = 0-3
MemoryMax = 4G
```

systemd транслирует это в `cpu.weight`, `cpu.max`, `cpuset.cpus`, `memory.max` соответствующей cgroup. Это правильный
способ конфигурировать лимиты на production-серверах — без ручного редактирования `/sys/fs/cgroup`.

### Автогруппы планировщика

Параллельно с cgroups в Linux есть упрощённый механизм **sched autogroups** — автоматическая группировка процессов по *
*session ID**:

```bash
cat /proc/sys/kernel/sched_autogroup_enabled   # 1 = включено по умолчанию
```

Когда autogroup включён, все процессы одной shell-сессии получают общий «бюджет» CPU. Это объясняет, почему `make -j64`,
запущенный в терминале, не блокирует interactive-сессию в другом терминале — у каждой свой autogroup.

Управлять nice-уровнем autogroup можно через `/proc/<pid>/autogroup`:

```bash
echo 10 > /proc/$$/autogroup     # понизить приоритет всей сессии
```

В отличие от cgroups, autogroups невидимы в `/sys/fs/cgroup` и не дают тонкого контроля — это «бесплатная» оптимизация
для типичного desktop-сценария.

## Связанные темы

- [Основы процессов](processes_basics.md) — UID/EUID и SUID-биты, которые взаимодействуют с capabilities
- [seccomp](../isolation/seccomp.md) — дополнительный механизм ограничения привилегий: фильтрация системных вызовов
- [fork и exec](fork_and_exec.md) — наследование capabilities и nice-приоритета дочерними процессами
- [rlimit](rlimit.md) — per-process лимиты, дополняющие cgroup-лимиты на группу

## Источники

- `man 2 getpriority` — getpriority, setpriority
- `man 1 nice` — запуск с изменённым nice
- `man 1 renice` — изменение nice работающего процесса
- `man 2 sched_setaffinity` — управление CPU affinity
- `man 1 taskset` — утилита для управления affinity
- `man 7 capabilities` — подробное описание всех capabilities Linux
- `man 3 cap_get_proc` — API libcap
- `man 8 setcap` — установка capabilities на файл
- `man 8 getcap` — просмотр capabilities файла
- `man 7 cgroups` — обзор cgroups v1 и v2
- `man 5 systemd.resource-control` — CPUWeight, CPUQuota, AllowedCPUs в unit-файлах
- `man 1 systemd-cgls`, `man 1 systemd-cgtop` — утилиты observability
- [Control Group v2 — kernel.org](https://www.kernel.org/doc/Documentation/admin-guide/cgroup-v2.rst) — официальная
  документация cgroups v2
- [CFS Bandwidth Control — kernel.org](https://www.kernel.org/doc/html/latest/scheduler/sched-bwc.html) — как cpu.max
  реализован в планировщике
