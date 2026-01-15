##### Расскажите про библиотеку seccomp.

**seccomp (secure computing)** -- это механизм фильтрации системных вызовов в Linux ядре. Позволяет ограничить, какие
сисколлы может делать процесс, повышая безопасность (особенно полезно для sandbox-изоляции).

**Уровни:**

1. **Mode 1 (strict)** -- запрещены все сисколлы кроме `read`, `write`, `_exit`, `sigreturn`;
2. **Mode 2 (filter)** -- можно задать свой набор разрешённых/запрещённых сисколлов через BPF-фильтры.

**Библиотека libseccomp** предоставляет удобный API вместо прямого написания BPF:

```c
#include <seccomp.h>

scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);  // по умолчанию всё разрешено
seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(execve), 0);  // запретить execve
seccomp_load(ctx);  // применить фильтр к процессу
seccomp_release(ctx);
```

##### Покажите на примере, как запретить программе вызывать определенные сисколлы.

Пример: запретить `fork` и `execve`:

```c
#define _GNU_SOURCE
#include <seccomp.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    printf("PID=%d\n", getpid());
    
    // Инициализировать фильтр (по умолчанию ALLOW)
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    
    // Добавить правила: запретить fork и execve
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(fork), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(execve), 0);
    
    // Загрузить фильтр в ядро
    if (seccomp_load(ctx) < 0) {
        perror("seccomp_load failed");
        return 1;
    }
    
    seccomp_release(ctx);
    
    printf("Seccomp filter applied\n");
    
    // Попытка fork -- будет убит процесс
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
    }
    
    return 0;
}
```

Компилировать:

```bash
gcc -o test test.c -lseccomp
./test
# Bad system call (core dumped) или Killed
```

**Более сложный пример с условиями:**

```c
// Запретить write в файл (fd > 2, т.е. не в stdout/stderr)
seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EACCES), SCMP_SYS(write),
                 1, SCMP_A0(SCMP_CMP_GT, 2));
```

##### Как получить ошибку Bad system call (core dumped)?

Ошибка `Bad system call (core dumped)` появляется, когда:

1. **Процесс вызывает запрещённый seccomp сисколл с действием `SCMP_ACT_KILL`** или `SCMP_ACT_KILL_PROCESS`;
2. **Сисколл не существует** или невозможен на данной архитектуре.

Пример 1 (seccomp):

```bash
# Как выше -- примерно фильтр, запрещающий fork
./test
# Bad system call (core dumped)
```

Пример 2 (несуществующий сисколл):

```c
#include <unistd.h>
#include <sys/syscall.h>

int main() {
    // Вызвать несуществующий сисколл (номер 999)
    syscall(999);
    return 0;
}
```

Пример 3 (из shell):

```bash
# Если ограничить через ulimit
ulimit -c unlimited
./prog_with_bad_syscall
```

**Где найти core-дамп:**

```bash
ls -la core*
gdb ./prog core          # анализировать дамп
```
