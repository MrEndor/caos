##### Что такое приоритет процесса, какой он бывает, как его узнать и как поменять?

**Приоритет процесса** определяет, как часто процесс получает процессорное время. В Linux есть два типа приоритетов:

**1. Nice-приоритет** (от -20 до 19):

- `-20` -- максимальный приоритет (самый "жадный" к CPU);
- `0` -- нормальный приоритет (по умолчанию);
- `19` -- минимальный приоритет.

Узнать nice-значение:

```bash
ps -o pid,nice,comm
# или
nice -p <pid>
```

Из кода:

```c
#include <sys/resource.h>
#include <unistd.h>

int nice_value = getpriority(PRIO_PROCESS, getpid());
printf("Nice = %d\n", nice_value);
```

Изменить nice-приоритет:

```bash
nice -n 10 ./prog              # запустить с nice=10
renice -n 5 -p <pid>           # изменить приоритет уже работающего процесса
```

Из кода:

```c
#include <sys/resource.h>

setpriority(PRIO_PROCESS, 0, 10);  // установить nice=10 текущему процессу
```

**2. Real-time приоритеты** (от 1 до 99, требуют привилегий):

- используются планировщиком `SCHED_FIFO` и `SCHED_RR`;
- выше любого nice-приоритета.

##### Что такое CPU affinity данного процесса, как его узнать и как поменять?

**CPU affinity** -- это маска, которая определяет, на каких ядрах CPU может выполняться процесс. Это нужно для:

- улучшения cache-локальности;
- привязки критичных по времени процессов к определённым ядрам;
- балансировки нагрузки.

Узнать affinity:

```bash
taskset -p <pid>               # текущее affinity
ps -eo pid,psr,comm             # PSR -- текущее ядро
```

Из кода:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

cpu_set_t mask;
sched_getaffinity(getpid(), sizeof(mask), &mask);

// Проверить, может ли процесс бежать на ядре 0
if (CPU_ISSET(getpid(), &mask)) {
    printf("Ядро 0 в affinity-маске\n");
}
```

Установить affinity:

```bash
taskset -cp 0,1 <pid>           # привязать к ядрам 0 и 1
taskset -cp 0 ./prog            # запустить на ядре 0
```

Из кода:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>

cpu_set_t mask;
CPU_ZERO(&mask);
CPU_SET(0, &mask);              // добавить ядро 0
CPU_SET(1, &mask);              // добавить ядро 1

sched_setaffinity(getpid(), sizeof(mask), &mask);  // установить маску
```

##### Что такое process capabilities в Linux, какие они бывают?

**Capabilities** -- это механизм дробления привилегий root на более мелкие права. Вместо бинарного "root / не-root"
можно выдать процессу только конкретные привилегии.

Основные capabilities:

- `CAP_SYS_ADMIN` -- административные привилегии;
- `CAP_NET_ADMIN` -- управление сетью;
- `CAP_SYS_TIME` -- изменение системного времени;
- `CAP_NET_BIND_SERVICE` -- привязка к портам < 1024;
- `CAP_DAC_OVERRIDE` -- игнорирование DAC-прав доступа;
- `CAP_SYS_CHROOT` -- использование `chroot`;
- `CAP_SETUID` -- смена UID через `setuid`;
- и много других...

Всего ~40 capabilities.

Процесс может иметь набор capabilities в трёх множествах (permitted, effective, inheritable). Это позволяет, например,
бинарю работать от непривилегированного пользователя, но с несколькими необходимыми возможностями.

##### Что такое Permitted set (набор разрешённых)?

**Permitted (разрешённые способности)** — это **максимальный набор способностей**, 
которые процесс **может использовать**.

**Свойства:**

- Это **максимум** — процесс не может сделать что-то за пределами этого набора
- Суперсет для **Effective** — все включённые в Effective должны быть в Permitted
- Суперсет для **Inheritable** — может быть больше, чем в Effective
- Определяется при **запуске процесса** (наследуется от родителя, файловых прав и т.д.)

**Аналогия:**

```
Permitted = билет в вип-клуб (даёт право на всё в клубе)
Effective = действующие права (что ты используешь сейчас)
Allowed  = всё, на что у тебя есть билет
```

**Пример:**

```cpp
#include <sys/capability.h>
#include <stdio.h>

int main() {
    cap_t caps = cap_get_proc();  // Получить capabilities процесса
    
    // Permitted: какие способности разрешены
    char *cap_text = cap_to_text(caps, NULL);
    printf("Capabilities: %s\n", cap_text);
    
    // Проверить конкретную способность
    cap_flag_value_t val;
    cap_get_flag(caps, CAP_NET_RAW, CAP_PERMITTED, &val);
    printf("CAP_NET_RAW (Permitted): %s\n", val ? "yes" : "no");
    
    cap_free(caps);
    return 0;
}
```

**Практический пример:**

```bash
# Процесс root имеет все capabilities в Permitted
sudo -i
cat /proc/self/status | grep Cap
# CapPrm: 000001ffffffffff (все способности разрешены)

# Обычный пользователь не имеет никаких
whoami  # user
cat /proc/self/status | grep Cap
# CapPrm: 0000000000000000 (ничего не разрешено)
```

---

##### Что такое Effective set (набор активных)?

**Effective (активные способности)** — это **текущий набор способностей**, которые процесс **действительно использует
прямо сейчас**.

**Свойства:**

- Это **реальные права** — то, что процесс может делать **в данный момент**
- Всегда **подмножество Permitted** — не может быть больше, чем разрешено
- Может **динамически меняться** — процесс может добавлять/удалять свои способности
- Используется для **проверки прав** при каждой операции

**Аналогия:**

```
Permitted = вся зарплата на банковском счёте
Effective = деньги в кошельке (что ты можешь потратить прямо сейчас)
```

**Пример динамического изменения:**

```cpp
#include <sys/capability.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    cap_t caps = cap_get_proc();
    
    // Получить текущее состояние Effective
    char *text = cap_to_text(caps, NULL);
    printf("Current Effective: %s\n", text);
    
    // Добавить способность в Effective
    cap_set_flag(caps, CAP_EFFECTIVE, 1, (const cap_value_t[]){CAP_NET_RAW}, CAP_SET);
    cap_set_proc(caps);  // Применить изменения
    
    printf("After adding CAP_NET_RAW to Effective\n");
    
    // Убрать способность из Effective
    cap_set_flag(caps, CAP_EFFECTIVE, 1, (const cap_value_t[]){CAP_NET_RAW}, CAP_CLEAR);
    cap_set_proc(caps);  // Применить изменения
    
    printf("After removing CAP_NET_RAW from Effective\n");
    
    cap_free(caps);
    return 0;
}
```

**Практический пример: ping без root**

```bash
# ping требует CAP_NET_RAW для создания raw сокетов
which ping
/usr/bin/ping

# По умолчанию обычный пользователь не может пингировать
ping 8.8.8.8
# ping: socket: Operation not permitted

# Разрешить обычному пользователю пинговать (добавить capability)
sudo setcap cap_net_raw=ep /usr/bin/ping

# Теперь работает!
ping 8.8.8.8
# PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.
# 64 bytes from 8.8.8.8: icmp_seq=1 ttl=119 time=10.2 ms

# Проверить, какие capability установлены
getcap /usr/bin/ping
# /usr/bin/ping = cap_net_raw+ep
# e = Effective
# p = Permitted
```

---

##### Что такое Inheritable set (набор наследуемых)?

**Inheritable (наследуемые способности)** — это **набор способностей, которые передаются дочерним процессам**.

**Свойства:**

- Определяет, какие способности **могут передаться при fork/exec**
- Дочерний процесс **не автоматически получает** все способности родителя
- Используется вместе с **файловыми Inheritable флагами** при exec
- Может быть **больше чем Effective** (способности в спящем режиме)

**Как работает наследование:**

```
Parent process (fork)
    ↓
Child process (наследует от родителя)
    ├─ Effective child = ?
    ├─ Permitted child = ?
    └─ Inheritable child = Parent's Inheritable

Если дочерний процесс делает exec(новая программа):
    Новая программа может получить способности на основе:
    1. Своих файловых Inheritable флагов
    2. Inheritable от родителя
    3. Ambient set (если установлен)
```

**Пример: наследование capabilities при fork/exec**

```cpp
#include <sys/capability.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    cap_t caps = cap_get_proc();
    
    // Добавить CAP_NET_RAW в Inheritable
    cap_set_flag(caps, CAP_INHERITABLE, 1, (const cap_value_t[]){CAP_NET_RAW}, CAP_SET);
    cap_set_proc(caps);
    
    printf("Parent - Inheritable capabilities set\n");
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        cap_t child_caps = cap_get_proc();
        
        // Проверить Inheritable в дочернем процессе
        cap_flag_value_t val;
        cap_get_flag(child_caps, CAP_NET_RAW, CAP_INHERITABLE, &val);
        printf("Child - CAP_NET_RAW in Inheritable: %s\n", val ? "yes" : "no");
        
        cap_free(child_caps);
        exit(0);
    } else {
        // Parent
        wait(NULL);
    }
    
    cap_free(caps);
    return 0;
}
```

**Практический пример: передача capabilities через setcap файловых флагов**

```bash
# Создать программу, требующую NET_RAW
cat > net_prog.c << 'EOF'
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main() {
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    printf("Raw socket created successfully\n");
    return 0;
}
EOF

gcc -o net_prog net_prog.c

# Без capabilities — не работает
./net_prog
# socket: Operation not permitted

# Установить capabilities с Inheritable флагом
sudo setcap cap_net_raw=eip ./net_prog
# i = Inheritable (передаётся дочерним процессам)

# Теперь работает
./net_prog
# Raw socket created successfully

# Проверить флаги
getcap ./net_prog
# ./net_prog = cap_net_raw+eip
# e = Effective
# i = Inheritable
# p = Permitted
```

---

##### Таблица: Permitted vs Effective vs Inheritable

| Характеристика    | Permitted          | Effective                    | Inheritable            |
|-------------------|--------------------|------------------------------|------------------------|
| **Назначение**    | Максимум прав      | Текущие действующие права    | Права для наследования |
| **Изменяемость**  | Сложно менять      | Легко менять процессом       | Сложно менять          |
| **Ограничение**   | Не ограничено      | ⊆ Permitted                  | Независим              |
| **Наследование**  | При fork/exec      | Не наследуется автоматически | Передаётся при exec    |
| **Проверка прав** | На стартовом этапе | При каждой операции          | При передаче процессам |
| **Значение**      | Суперсет всех прав | Реально используемые права   | Спящие права           |

---

##### Как выдать процессу определенные capabilities, как посмотреть имеющиеся?

Посмотреть capabilities:

```bash
getcap /usr/bin/ping            # для файла
getpcaps <pid>                  # для работающего процесса
```

Из кода:

```c
#include <sys/capability.h>

cap_t caps = cap_get_proc();    // получить capabilities текущего процесса
cap_clear(caps);
cap_set_flag(caps, CAP_EFFECTIVE, 1, (cap_value_t[]){CAP_NET_BIND_SERVICE}, CAP_SET);
cap_set_proc(caps);             // установить

cap_free(caps);
```

Выдать capabilities файлу:

```bash
setcap cap_net_bind_service=ep /usr/local/bin/myserver
# ep = effective + permitted (или cap_net_bind_service+i для inherited)

# Проверить:
getcap /usr/local/bin/myserver
# Результат: /usr/local/bin/myserver = cap_net_bind_service+ep
```

Запустить процесс с нужными capabilities:

```bash
# Через файл (как выше)
# Либо через setcap, либо запустить от root и внутри приложения подписать capabilities

# Или через sudo с сохранением capabilities:
sudo -E /path/to/prog
```

**Важно:** capabilities наследуются в зависимости от типа (inherited/effective/permitted).
