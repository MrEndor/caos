##### Что такое rlimit для процесса?

**rlimit (resource limit)** -- это ограничения на потребление ресурсов процессом, выставляемые ядром. Каждый процесс
имеет "soft limit" и "hard limit":

- **soft limit** -- текущее ограничение; процесс получит сигнал (обычно `SIGXCPU`), если превысит;
- **hard limit** -- максимально возможный лимит для данного пользователя; нельзя увеличить без привилегий.

Основные лимиты (`RLIMIT_*`):

- `RLIMIT_CPU` -- максимальное процессорное время (в секундах);
- `RLIMIT_FSIZE` -- максимальный размер файла, который может создать процесс;
- `RLIMIT_DATA` -- максимальный размер данных (heap);
- `RLIMIT_STACK` -- максимальный размер стека;
- `RLIMIT_NOFILE` -- максимальное количество открытых файловых дескрипторов;
- `RLIMIT_NPROC` -- максимальное количество процессов, которые может создать пользователь;
- `RLIMIT_MEMLOCK` -- максимальный размер памяти, которую можно залочить;
- `RLIMIT_CORE` -- максимальный размер core-дампа.

##### Как пользоваться функциями getrlimit и setrlimit?

```c
#include <sys/resource.h>

struct rlimit {
    rlim_t rlim_cur;   // soft limit
    rlim_t rlim_max;   // hard limit
};

// Получить текущий лимит на открытые файлы
struct rlimit lim;
getrlimit(RLIMIT_NOFILE, &lim);
printf("Soft: %ld, Hard: %ld\n", lim.rlim_cur, lim.rlim_max);

// Установить новый лимит
lim.rlim_cur = 2048;    // увеличить soft limit
setrlimit(RLIMIT_NOFILE, &lim);
```

Из терминала:

```bash
ulimit -a             # показать все лимиты
ulimit -n             # показать лимит на файловые дескрипторы
ulimit -n 1024        # установить новый лимит (для текущей shell)
ulimit -c unlimited   # разрешить неограниченные core-дампы
```

##### Покажите, как из кода программы запросить себе больший размер стека, чем дан изначально.

```c
#include <sys/resource.h>
#include <stdio.h>

int main() {
    struct rlimit lim;
    
    // Получить текущий лимит на стек
    getrlimit(RLIMIT_STACK, &lim);
    printf("Текущий soft limit: %ld bytes\n", lim.rlim_cur);
    printf("Hard limit: %ld bytes\n", lim.rlim_max);
    
    // Установить больший soft limit (до hard limit)
    lim.rlim_cur = 128 * 1024 * 1024;  // 128 MB
    
    if (setrlimit(RLIMIT_STACK, &lim) == 0) {
        printf("Successfully increased stack size\n");
    } else {
        perror("setrlimit failed");
    }
    
    // Проверить
    getrlimit(RLIMIT_STACK, &lim);
    printf("New soft limit: %ld bytes\n", lim.rlim_cur);
    
    return 0;
}
```

**Важно:** для увеличения hard limit нужны привилегии root. Soft limit можно увеличить до hard limit.

##### Как установить процессу ограничение на использование памяти и/или процессорного времени?

**Ограничение памяти (heap):**

```c
#include <sys/resource.h>

struct rlimit lim;
lim.rlim_cur = 100 * 1024 * 1024;  // 100 MB
lim.rlim_max = 100 * 1024 * 1024;

setrlimit(RLIMIT_AS, &lim);  // RLIMIT_AS = virtual memory
// или RLIMIT_DATA для heap-памяти
```

Из shell:

```bash
ulimit -v 102400          # ограничить виртуальную память (в KB)
./prog
```

**Ограничение CPU-времени:**

```c
struct rlimit lim;
lim.rlim_cur = 10;        // 10 секунд CPU-времени
lim.rlim_max = 10;

setrlimit(RLIMIT_CPU, &lim);
```

Из shell:

```bash
ulimit -t 10              # max CPU time = 10 seconds
./prog
```

##### Что произойдет, если эти ограничения будут превышены?

- **RLIMIT_STACK переполнен** → `SIGSEGV` (stack overflow);
- **RLIMIT_DATA переполнен** → `brk()` или `malloc()` вернут ошибку;
- **RLIMIT_CPU превышен** → процесс получает `SIGXCPU`, обычно завершается;
- **RLIMIT_NOFILE** → попытка открыть новый файл/сокет вернёт ошибку `EMFILE`;
- **RLIMIT_NPROC** → `fork()` вернёт ошибку `EAGAIN`;
- **RLIMIT_CORE** → core-дамп может быть обрезан.

Пример переполнения CPU-лимита:

```bash
ulimit -t 5
while true; do :; done    # бесконечный цикл
# Через 5 секунд процесс получит SIGXCPU и завершится
```
