# Ограничение ресурсов процесса: rlimit

## Что такое rlimit

**rlimit (resource limit)** — это механизм ядра для ограничения потребления ресурсов процессом. Каждый ресурс имеет два
предела:

- **soft limit** (`rlim_cur`) — текущее действующее ограничение. Процесс может уменьшить или увеличить его
  самостоятельно (но не выше hard limit).
- **hard limit** (`rlim_max`) — абсолютный максимум. Увеличить его может только процесс с привилегией
  `CAP_SYS_RESOURCE` (обычно root).

Значение `RLIM_INFINITY` означает отсутствие ограничения.

## Основные типы лимитов

| Константа        | Описание                                                     |
|------------------|--------------------------------------------------------------|
| `RLIMIT_CPU`     | Максимальное процессорное время (секунды)                    |
| `RLIMIT_FSIZE`   | Максимальный размер создаваемого файла (байты)               |
| `RLIMIT_DATA`    | Максимальный размер сегмента данных (heap)                   |
| `RLIMIT_STACK`   | Максимальный размер стека                                    |
| `RLIMIT_CORE`    | Максимальный размер core-дампа                               |
| `RLIMIT_NOFILE`  | Максимальное количество открытых файловых дескрипторов       |
| `RLIMIT_NPROC`   | Максимальное количество процессов одного пользователя        |
| `RLIMIT_AS`      | Максимальный размер виртуального адресного пространства      |
| `RLIMIT_MEMLOCK` | Максимальный объём памяти, который можно заблокировать в RAM |

## Функции getrlimit и setrlimit

```c
#include <sys/resource.h>

struct rlimit {
    rlim_t rlim_cur;   // soft limit
    rlim_t rlim_max;   // hard limit
};

// Получить текущий лимит на открытые файловые дескрипторы
struct rlimit lim;
getrlimit(RLIMIT_NOFILE, &lim);
printf("Soft: %ld, Hard: %ld\n", (long)lim.rlim_cur, (long)lim.rlim_max);

// Увеличить soft limit
lim.rlim_cur = 4096;
if (setrlimit(RLIMIT_NOFILE, &lim) != 0) {
    perror("setrlimit");
}
```

Из командной строки через `ulimit`:

```bash
ulimit -a             # показать все текущие лимиты
ulimit -n             # лимит на файловые дескрипторы
ulimit -n 4096        # установить новый лимит (для текущей оболочки)
ulimit -c unlimited   # разрешить неограниченные core-дампы
ulimit -t 10          # ограничить CPU-время до 10 секунд
ulimit -v 524288      # ограничить виртуальную память до 512 МБ
```

## Увеличение размера стека

По умолчанию стек ограничен (обычно 8 МБ). Если программа использует глубокую рекурсию или большие локальные массивы,
лимит можно увеличить до hard limit:

```c
#include <sys/resource.h>
#include <stdio.h>

int main() {
    struct rlimit lim;

    // Узнать текущие ограничения
    getrlimit(RLIMIT_STACK, &lim);
    printf("Soft limit: %ld bytes\n", (long)lim.rlim_cur);
    printf("Hard limit: %ld bytes\n", (long)lim.rlim_max);

    // Запросить 128 МБ (должно быть <= hard limit)
    lim.rlim_cur = 128 * 1024 * 1024;

    if (setrlimit(RLIMIT_STACK, &lim) == 0) {
        printf("Stack size increased to 128 MB\n");
    } else {
        perror("setrlimit failed");
    }

    getrlimit(RLIMIT_STACK, &lim);
    printf("New soft limit: %ld bytes\n", (long)lim.rlim_cur);

    return 0;
}
```

## Ограничение памяти и CPU-времени

### Ограничение виртуальной памяти

```c
#include <sys/resource.h>

struct rlimit lim;
lim.rlim_cur = 100 * 1024 * 1024;  // 100 МБ
lim.rlim_max = 100 * 1024 * 1024;

// RLIMIT_AS — всё виртуальное адресное пространство
setrlimit(RLIMIT_AS, &lim);
```

### Ограничение CPU-времени

```c
#include <sys/resource.h>

struct rlimit lim;
lim.rlim_cur = 10;  // 10 секунд CPU-времени (soft)
lim.rlim_max = 15;  // 15 секунд (hard)

setrlimit(RLIMIT_CPU, &lim);
```

## Что происходит при превышении лимита

| Лимит                       | Последствие превышения                                       |
|-----------------------------|--------------------------------------------------------------|
| `RLIMIT_CPU` (soft)         | Процесс получает `SIGXCPU`                                   |
| `RLIMIT_CPU` (hard)         | Процесс получает `SIGKILL`                                   |
| `RLIMIT_FSIZE`              | При записи в файл — `SIGXFSZ`; `write()` возвращает `EFBIG`  |
| `RLIMIT_STACK`              | `SIGSEGV` (stack overflow)                                   |
| `RLIMIT_DATA` / `RLIMIT_AS` | `brk()` / `mmap()` / `malloc()` возвращают ошибку (`ENOMEM`) |
| `RLIMIT_NOFILE`             | `open()` / `socket()` возвращают `EMFILE`                    |
| `RLIMIT_NPROC`              | `fork()` возвращает `EAGAIN`                                 |
| `RLIMIT_CORE`               | Core-дамп будет обрезан или не создан                        |

Пример проверки превышения CPU-лимита из командной строки:

```bash
ulimit -t 5
# Запустить бесконечный цикл:
while true; do :; done
# Через 5 секунд CPU-времени процесс получит SIGXCPU и завершится
```

## prlimit: rlimit для другого процесса

`prlimit(2)` — более гибкая версия `setrlimit`, позволяющая читать и устанавливать лимиты для **любого** процесса по
PID (при наличии прав):

```c
#include <sys/resource.h>

struct rlimit lim;
// Получить лимит для процесса с PID pid (0 = текущий процесс)
prlimit(pid, RLIMIT_NOFILE, NULL, &lim);

// Установить лимит для другого процесса
lim.rlim_cur = 1024;
prlimit(pid, RLIMIT_NOFILE, &lim, NULL);
```

Из командной строки: `prlimit --nofile=1024 --pid <pid>`

## Связанные темы

- [seccomp](../isolation/seccomp.md) — дополнительный механизм изоляции: фильтрация системных вызовов
- [fork и exec](fork_and_exec.md) — лимиты наследуются дочерними процессами через fork; fork-бомба как пример исчерпания
  `RLIMIT_NPROC`
- [Сигналы](signals.md) — `SIGXCPU` и `SIGXFSZ` как реакция на превышение лимитов

## Источники

- `man 2 getrlimit` — getrlimit, setrlimit, prlimit
- `man 1 ulimit` — управление лимитами из оболочки
- `man 7 signal` — список сигналов (SIGXCPU, SIGXFSZ)
- `man 5 limits.conf` — конфигурация системных лимитов через PAM
