# seccomp: фильтрация системных вызовов

## Что такое seccomp

**seccomp** (secure computing) — механизм ядра Linux, позволяющий ограничить набор системных вызовов, которые может
выполнять процесс. Это инструмент изоляции и защиты: если процесс скомпрометирован, seccomp не позволит злоумышленнику
использовать опасные системные вызовы.

seccomp активно применяется в контейнерных технологиях (Docker, systemd), браузерах (Chrome/Chromium) и других
приложениях, где требуется sandbox-изоляция.

### Режимы работы

**Strict mode (режим 1):** разрешены только четыре системных вызова — `read`, `write`, `_exit`, `sigreturn`. Любой
другой вызов немедленно завершает процесс сигналом `SIGKILL`. Включается через
`prctl(PR_SET_SECCOMP, SECCOMP_MODE_STRICT)`.

**Filter mode (режим 2):** позволяет задать гибкий набор правил с помощью BPF-программ. Это основной режим, используемый
на практике. Включается через `prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, ...)` или через libseccomp.

### BPF filter pipeline

При каждом системном вызове ядро прогоняет его через цепочку фильтров. Каждый фильтр — BPF-программа, которая
анализирует `struct seccomp_data` и возвращает вердикт:

```
  Процесс вызывает syscall (например, open)
          │
          ▼
  ┌───────────────────────────────────────────────────────────┐
  │  ядро: точка входа системного вызова                      │
  │                                                           │
  │  struct seccomp_data {                                    │
  │      nr   = __NR_open           ← номер syscall           │
  │      arch = AUDIT_ARCH_X86_64   ← архитектура             │
  │      args = [fd, path, flags…]  ← аргументы               │
  │  }                                                        │
  └─────────────────────┬─────────────────────────────────────┘
                        │
                        ▼
  ┌───────────────────────────────────────────────────────────┐
  │  BPF filter 1 (первый загруженный, выполняется последним) │
  │    BPF инструкции: load nr → compare → jump               │
  │              возвращает один из вердиктов                 │
  └─────────────────────┬─────────────────────────────────────┘
                        │  (если вердикт не KILL/ERRNO — следующий)
                        ▼
  ┌───────────────────────────────────────────────────────────┐
  │  BPF filter N (последний загруженный, выполняется первым) │
  └─────────────────────┬─────────────────────────────────────┘
                        │
                        ▼
             ┌──────────────────┐
             │    вердикт       │
             └────────┬─────────┘
           ┌──────────┼──────────────────────────────┐
           │          │          │          │         │
           ▼          ▼          ▼          ▼         ▼
       ALLOW       KILL      KILL_PROCESS  ERRNO(e)  TRAP
    ┌─────────┐ ┌────────┐  ┌───────────┐ ┌───────┐ ┌───────┐
    │syscall  │ │убить   │  │убить весь │ │вернуть│ │SIGSYS │
    │выполняет│ │поток   │  │процесс    │ │-e из  │ │для    │
    │ся штатно│ │(SIGSYS)│  │(SIGKILL)  │ │syscall│ │отладки│
    └─────────┘ └────────┘  └───────────┘ └───────┘ └───────┘
```

Если загружено несколько фильтров (вложенные `seccomp_load`), ядро выполняет их все и берёт наиболее ограничительный
вердикт: `KILL > TRAP > ERRNO > LOG > ALLOW`.

## Библиотека libseccomp

Написание BPF-фильтров вручную сложно и чревато ошибками. Библиотека **libseccomp** предоставляет удобный
высокоуровневый API.

Основные функции:

- `seccomp_init(action)` — создать контекст фильтра; `action` — действие по умолчанию для всех не упомянутых системных
  вызовов;
- `seccomp_rule_add(ctx, action, syscall, arg_cnt, ...)` — добавить правило для конкретного системного вызова;
- `seccomp_load(ctx)` — применить фильтр к текущему процессу;
- `seccomp_release(ctx)` — освободить контекст.

Действия (`action`):

| Константа               | Описание                         |
|-------------------------|----------------------------------|
| `SCMP_ACT_ALLOW`        | Разрешить системный вызов        |
| `SCMP_ACT_KILL_PROCESS` | Завершить процесс (`SIGSYS`)     |
| `SCMP_ACT_KILL`         | Завершить поток                  |
| `SCMP_ACT_ERRNO(e)`     | Вернуть ошибку с кодом `e`       |
| `SCMP_ACT_TRAP`         | Отправить `SIGSYS` (для отладки) |
| `SCMP_ACT_LOG`          | Записать в журнал, но разрешить  |

## Запрет отдельных системных вызовов

Простейший пример — начать с «всё разрешено» и добавить запреты:

```c
#define _GNU_SOURCE
#include <seccomp.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    printf("PID=%d\n", getpid());

    // Инициализировать фильтр: по умолчанию всё разрешено
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);

    // Запретить fork и execve
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(fork), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(execve), 0);

    // Загрузить фильтр в ядро
    if (seccomp_load(ctx) < 0) {
        perror("seccomp_load failed");
        return 1;
    }

    seccomp_release(ctx);

    printf("Seccomp filter applied\n");

    // Попытка вызвать fork — процесс будет немедленно завершён
    pid_t child = fork();
    if (child < 0) {
        perror("fork");  // до этого не дойдёт
    }

    return 0;
}
```

Компиляция и запуск:

```bash
gcc -o test test.c -lseccomp
./test
# Bad system call (core dumped)
```

## Whitelist-подход: разрешить только нужные вызовы

Более безопасная стратегия — начать с «всё запрещено» и добавить только необходимые системные вызовы:

```c
#include <seccomp.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    // По умолчанию — убивать за любой системный вызов
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);

    // Разрешить только минимально необходимые вызовы
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);

    seccomp_load(ctx);
    seccomp_release(ctx);

    // Теперь можно только читать и писать
    write(1, "Hello\n", 6);

    // Любой другой системный вызов (например, open) убьёт процесс
    return 0;
}
```

## Фильтрация с условиями на аргументы

seccomp позволяет проверять не только номер системного вызова, но и значения его аргументов:

```c
// Запретить write() в файловые дескрипторы с номером > 2
// (разрешить только запись в stdin/stdout/stderr)
seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EACCES), SCMP_SYS(write),
                 1,
                 SCMP_A0(SCMP_CMP_GT, 2));  // аргумент 0 (fd) > 2
```

## Ошибка «Bad system call»

Сигнал `SIGSYS` (сигнал 31) и сообщение «Bad system call (core dumped)» возникают в двух ситуациях:

1. **Процесс нарушил seccomp-фильтр** с действием `SCMP_ACT_KILL` или `SCMP_ACT_KILL_PROCESS`.
2. **Программа вызвала несуществующий системный вызов** (например, с недопустимым номером).

Пример несуществующего системного вызова:

```c
#include <unistd.h>
#include <sys/syscall.h>

int main() {
    syscall(999);  // системного вызова с номером 999 не существует
    return 0;
}
```

Анализ core-дампа:

```bash
ulimit -c unlimited     # разрешить создание core-дампа
./bad_prog
ls core*                # найти дамп
gdb ./bad_prog core     # запустить отладчик
```

## Прямое написание BPF-фильтра

libseccomp — это обёртка над низкоуровневым BPF API. Фильтр представляет собой массив инструкций `struct sock_filter`.
Каждая инструкция — это операция над полем структуры `seccomp_data`, которую ядро передаёт фильтру при каждом системном
вызове:

```c
struct seccomp_data {
    int   nr;         // номер системного вызова
    __u32 arch;       // архитектура (AUDIT_ARCH_X86_64 и т.д.)
    __u64 instruction_pointer;  // адрес инструкции syscall
    __u64 args[6];    // аргументы системного вызова
};
```

Пример минимального BPF-фильтра (разрешить только `write` и `exit_group`):

```c
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <stddef.h>
#include <unistd.h>

int main() {
    struct sock_filter filter[] = {
        // Загрузить номер архитектуры
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, arch)),
        // Проверить, что это x86-64; иначе — убить
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),

        // Загрузить номер системного вызова
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                 offsetof(struct seccomp_data, nr)),
        // Разрешить write
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_write, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        // Разрешить exit_group
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit_group, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        // Все остальные — убить
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL),
    };

    struct sock_fprog prog = {
        .len = sizeof(filter) / sizeof(filter[0]),
        .filter = filter,
    };

    // PR_SET_NO_NEW_PRIVS обязателен: запрещает получение новых привилегий
    // (без него непривилегированный процесс не может загрузить фильтр)
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);

    write(1, "allowed\n", 8);
    return 0;
    // open() после установки фильтра убьёт процесс
}
```

О том, как ядро выполняет системные вызовы до того как фильтр их перехватит,
см. [Введение в системные вызовы](../assembler_and_processor/system_calls_introduction.md).

## Связанные темы

- [Введение в системные вызовы](../assembler_and_processor/system_calls_introduction.md) — механизм системных вызовов,
  которые seccomp фильтрует
- [rlimit](../processes/rlimit.md) — другой механизм ограничения ресурсов процесса
- [Приоритеты, аффинность, capabilities](../processes/process_priority_affinity_capabilities.md) — capabilities как
  дополнение к seccomp для разграничения привилегий

## Источники

- `man 2 seccomp` — системный вызов seccomp
- `man 2 prctl` — управление параметрами процесса (PR_SET_SECCOMP)
- `man 3 seccomp_init` — API libseccomp
- [libseccomp GitHub](https://github.com/seccomp/libseccomp) — исходный код и документация
- Docker seccomp profiles — примеры реальных профилей изоляции
