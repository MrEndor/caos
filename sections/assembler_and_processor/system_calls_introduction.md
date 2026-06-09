# Системные вызовы: введение

## Что такое системный вызов

**Системный вызов (syscall)** — механизм перехода из пользовательского режима (user space) в режим ядра (kernel space),
через который процесс просит операционную систему выполнить привилегированное действие:

- чтение и запись файлов и устройств;
- работа с сетью (создание сокетов, отправка данных);
- управление памятью (`mmap`, `brk`);
- управление процессами (`fork`, `execve`, `wait`);
- работа с таймерами, сигналами и т.д.

Обычные функции стандартной библиотеки (`read`, `write`, `open`, `fork`, `execve` и др.) являются **тонкими обёртками**
над системными вызовами: внутри они выполняют инструкцию `syscall` (на x86-64), переключая процессор в привилегированный
режим.

### Как это работает на уровне процессора (x86-64)

Соглашение о вызовах системных вызовов на x86-64 Linux:

| Регистр                                | Назначение                           |
|----------------------------------------|--------------------------------------|
| `rax`                                  | Номер syscall                        |
| `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` | Аргументы (до 6)                     |
| `rax` (возврат)                        | Результат (отрицательный при ошибке) |

Механизм перехода из user mode в kernel mode и обратно:

```
  User space (Ring 3)                  Kernel space (Ring 0)
  ─────────────────────────────        ──────────────────────────────────────
  write(1, buf, n)
        │
        ▼
  libc wrapper:
    rax = 1          ; SYS_write
    rdi = 1          ; fd = stdout
    rsi = buf
    rdx = n
    syscall  ────────────────────────▶ CPU аппаратно:
                                         rcx ← rip   (адрес возврата)
                                         r11 ← rflags
                                         CS/SS ← Ring 0 selectors
                                         rip ← MSR_LSTAR

                                       ┌────────────────────────────────┐
                                       │ entry_SYSCALL_64 (asm)         │
                                       │   swapgs                       │
                                       │   переключить на kernel stack  │
                                       │   push pt_regs (все регистры)  │
                                       │   проверить rax < NR_syscalls  │
                                       │   call sys_write(rdi, rsi, rdx)│
                                       │   pop  pt_regs                 │
                                       │   rax ← результат / -errno     │
                                       └────────────────────────────────┘

                                       sysretq
        ◀────────────────────────────    CPL: 0 → 3
                                         rip    ← rcx
                                         rflags ← r11
  libc: возврат rax
    rax < 0  →  errno = -rax; return -1
    иначе    →  return rax  (число записанных байт)
        │
        ▼
  Программа продолжает
```

Ключевые особенности `syscall`/`sysret` (в отличие от прерывания `int 0x80`):

- `rcx` и `r11` **перезаписываются** процессором (адрес возврата и флаги); не передавай через них аргументы
- адрес обработчика берётся из MSR `LSTAR` (Model Specific Register), не из IDT — это быстрее
- на `sysret` нельзя вернуться, если `rcx` содержит неканонический адрес (ядро это проверяет)

## Трассировка системных вызовов: strace

`strace` перехватывает все системные вызовы программы и печатает их с именами, аргументами и возвращаемыми значениями:

```bash
strace ./prog                        # запустить и трассировать
strace -p 12345                      # подключиться к процессу по PID
strace -e trace=read,write ./prog    # только read и write
strace -f ./prog                     # включая дочерние процессы
strace -o log.txt ./prog             # писать в файл
strace -c ./prog                     # статистика по syscall'ам
```

Пример вывода:

```
read(0, "hello\n", 4096)  = 6
write(1, "hello\n", 6)    = 6
```

## Документация системных вызовов

Системные вызовы документированы в **разделе 2** man-страниц:

```bash
man 2 read
man 2 write
man 2 open
man 2 mmap
man 2 fork
man 2 errno   # коды ошибок
```

## Использование read и write

Прототипы:

```c
#include <unistd.h>

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
```

- `fd` — файловый дескриптор (0 — stdin, 1 — stdout, 2 — stderr);
- `buf` — указатель на буфер;
- `count` — максимальное количество байт.

Возвращаемые значения:

- `read` возвращает `1..count` — сколько байт реально прочитано; `0` — EOF; `-1` — ошибка.
- `write` возвращает `1..count` — сколько байт реально записано; `-1` — ошибка.

`read` и `write` могут вернуть **меньше байт**, чем запрошено («короткое чтение/запись»). Это нормально при работе с
терминалами, сокетами и pipe. Поэтому нужны циклы.

Пример программы `echo` на чистых системных вызовах:

```c
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

int main(void) {
    char buf[4096];

    while (1) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n == 0)       /* EOF */
            break;
        if (n == -1) {
            perror("read");
            return 1;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(1, buf + written, n - written);
            if (m == -1) {
                perror("write");
                return 1;
            }
            written += m;
        }
    }

    return 0;
}
```

## Обработка ошибок: errno

При ошибке системные вызовы возвращают `-1` и устанавливают **thread-local** переменную `errno` с кодом ошибки.

Шаблон правильной обработки:

```c
#include <errno.h>
#include <stdio.h>
#include <string.h>

ssize_t res = read(fd, buf, size);
if (res == -1) {
    int err = errno;   /* сохранить сразу — следующий вызов может затереть */
    fprintf(stderr, "read: %s (errno=%d)\n", strerror(err), err);
    /* или просто: */
    perror("read");
}
```

Часто встречающиеся коды ошибок:

| Код                      | Значение                                        |
|--------------------------|-------------------------------------------------|
| `EINTR`                  | Вызов прерван сигналом — обычно нужно повторить |
| `EAGAIN` / `EWOULDBLOCK` | Нет данных на неблокирующем дескрипторе         |
| `EBADF`                  | Неверный файловый дескриптор                    |
| `EFAULT`                 | Неверный адрес буфера                           |
| `EACCES`                 | Нет прав доступа                                |
| `ENOENT`                 | Файл не найден                                  |

## Прямой вызов syscall из C

В случаях когда нет обёртки в libc, можно вызвать syscall напрямую через функцию `syscall(2)`:

```c
#include <sys/syscall.h>
#include <unistd.h>

/* Аналог getpid() без libc-обёртки */
pid_t pid = (pid_t)syscall(SYS_getpid);
```

Или на x86-64 через inline-ассемблер (полезно в загрузчиках и коде без libc):

```c
#include <stdint.h>

static inline long raw_write(int fd, const void *buf, long count) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(1L /* SYS_write */), "D"((long)fd), "S"(buf), "d"(count)
        : "rcx", "r11", "memory"
    );
    return ret;
}
```

## Связанные темы

- [Процессы: основы](../processes/processes_basics.md) — процессы как субъекты системных вызовов
- [Файловые дескрипторы](../files_and_filesystems/file_descriptors.md) — `read`/`write` и другие файловые syscall'ы
- [Запуск и завершение программы](../linking_and_libraries/program_startup_shutdown.md) — `execve` и `exit_group` как
  примеры системных вызовов

## Источники

- `man 2 read` — системный вызов read
- `man 2 write` — системный вызов write
- `man 2 open` — открытие файлов
- `man 2 syscall` — низкоуровневый вызов произвольного syscall
- `man 3 errno` — коды ошибок
- `man strace` — трассировка системных вызовов
- [Linux syscall table (x86-64)](https://filippo.io/linux-syscall-table/) — таблица номеров системных вызовов
