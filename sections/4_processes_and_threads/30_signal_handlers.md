##### Как сделать кастомный обработчик сигналов?

**Способ 1: signal()** (старый, простой, но не очень надёжный):

```c
#include <signal.h>
#include <stdio.h>

void my_handler(int sig) {
    printf("Получил сигнал %d\n", sig);
}

int main() {
    signal(SIGUSR1, my_handler);    // установить обработчик
    
    pause();                         // ждать сигнала
    return 0;
}
```

**Способ 2: sigaction()** (новый, рекомендуемый):

```c
#include <signal.h>
#include <stdio.h>

void my_handler(int sig, siginfo_t *info, void *context) {
    printf("Сигнал %d от процесса %d\n", sig, info->si_pid);
}

int main() {
    struct sigaction sa;
    sa.sa_sigaction = my_handler;     // обработчик (с доп. аргументами)
    sa.sa_flags = SA_SIGINFO;         // требуется для sa_sigaction
    sigemptyset(&sa.sa_mask);         // какие сигналы блокировать
    
    sigaction(SIGUSR1, &sa, NULL);
    
    pause();
    return 0;
}
```

##### Покажите на примере, как из кода программы перехватывать segfault и делать что-то нестандартное при его наступлении.

```c
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void segfault_handler(int sig, siginfo_t *info, void *context) {
    printf("=== SEGFAULT CAUGHT ===\n");
    printf("Bad address: %p\n", info->si_addr);
    printf("Trying to recover...\n");
    
    // Можно попытаться восстановиться, но это опасно
    // Обычно просто логируем и завершаемся
    _exit(1);
}

int main() {
    struct sigaction sa;
    sa.sa_sigaction = segfault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGSEGV, &sa, NULL);
    
    printf("Установлен обработчик SIGSEGV\n");
    
    // Попытка dereferencia null pointer
    int *ptr = NULL;
    printf("Value: %d\n", *ptr);      // SIGSEGV!
    
    return 0;
}
```

Хотя в реальной программе восстановление после SIGSEGV опасно -- лучше просто логировать и завершаться.

##### Что, если во время обработки сигнала приходит другой сигнал?

По умолчанию:

1. **Если одинаковый сигнал приходит во время его обработки** -- второй экземпляр сохраняется в очереди (если это
   realtime-сигнал) или игнорируется (если обычный);
2. **Если другой сигнал** -- обычно **вложенная обработка**: новый сигнал прерывает текущий обработчик и выполняется
   новый, потом возврат в первый.

Это **может привести к ошибкам**, если обработчики используют общие структуры данных.

##### Как можно заблокировать получение других сигналов во время обработки сигнала?

Через маску блокировки в `sigaction`:

```c
#include <signal.h>

struct sigaction sa;
sa.sa_handler = my_handler;

// Установить маску: какие сигналы блокировать во время обработки
sigemptyset(&sa.sa_mask);
sigaddset(&sa.sa_mask, SIGUSR2);      // заблокировать SIGUSR2
sigaddset(&sa.sa_mask, SIGUSR1);      // заблокировать SIGUSR1

sa.sa_flags = 0;

sigaction(SIGUSR1, &sa, NULL);
```

Во время обработки SIGUSR1 сигналы SIGUSR2 и SIGUSR1 будут **отложены** и обработаны после выхода из обработчика.

**Альтернатива: блокировать все сигналы:**

```c
sigfillset(&sa.sa_mask);              // заблокировать все
```

##### Что, если сигнал приходит во время выполнения сисколла?

Результат зависит от сисколла и флагов обработчика:

1. **Медленные сисколлы** (read, write, wait и т.д.):
    - обычно **прерываются**, возвращают `-1` с `errno = EINTR`;
    - нужна **перезагрузка** сисколла в цикле.

2. **С флагом `SA_RESTART`** -- ядро автоматически перезагружает прерванный сисколл.

Пример обработки `EINTR`:

```c
#include <unistd.h>
#include <errno.h>

while (1) {
    ssize_t n = read(fd, buf, size);
    if (n == -1 && errno == EINTR) {
        continue;                       // перезагрузить read
    }
    if (n == -1) {
        perror("read failed");
        break;
    }
    // обработать данные
}
```

Или с `SA_RESTART`:

```c
sa.sa_flags = SA_RESTART;              // ядро перезагрузит сисколл
sigaction(SIGUSR1, &sa, NULL);
```

##### Что такое signal-safety и что такое реентрабельная функция?

**Signal-safety** -- функция, которая безопасна для вызова из обработчика сигнала. Список небольшой: `write`, `signal`,
`kill`, `exit` и т.д.

**Опасные** функции (не signal-safe):

- `printf`, `malloc`, `free` (используют статические буферы и lock'и);
- функции, которые вызывают signal-unsafe функции.

**Реентрабельная функция** -- функция, которая может быть безопасно вызвана несколько раз одновременно (из разных
потоков или при вложенной обработке) без корруптирования своего состояния.

Пример **не-реентрабельной** функции:

```c
// Опасно! Статический буфер
char *my_strtok(const char *str, const char *delim) {
    static char *buffer = NULL;  // проблема!
    // ...
}
```

Вызовите из основного кода, а во время выполнения обработчик сигнала вызовет ту же функцию -- будут проблемы.

**Правило для обработчиков сигналов:**

- использовать только signal-safe функции;
- избегать динамической памяти (`malloc`, `free`);
- не вызывать обычные функции `libc`;
- установить флаги и выйти (или вызвать `_exit`).

Пример безопасного обработчика:

```c
volatile sig_atomic_t got_signal = 0;

void safe_handler(int sig) {
    got_signal = 1;          // только простое присваивание
}

int main() {
    signal(SIGUSR1, safe_handler);
    
    while (!got_signal) {
        pause();
    }
    
    // Здесь безопасно делать всё что угодно
    printf("Обработано в основном коде\n");
    return 0;
}
```
