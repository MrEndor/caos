# Перенаправление ввода-вывода

## Потоки stdin, stdout и stderr

Каждый процесс имеет три стандартных потока ввода-вывода, связанных с предопределёнными файловыми дескрипторами:

| Поток  | FD | Назначение               |
|--------|----|--------------------------|
| stdin  | 0  | Стандартный ввод         |
| stdout | 1  | Стандартный вывод        |
| stderr | 2  | Стандартный вывод ошибок |

В C++ `cout` пишет в stdout (FD 1), `cerr` — в stderr (FD 2). По умолчанию все три потока унаследованы от терминала,
однако оболочка (shell) умеет перенаправлять их куда угодно ещё до запуска программы.

## Перенаправление в bash

Оператор `>` перенаправляет stdout в файл с перезаписью, `>>` — с добавлением в конец:

```bash
echo "Hello caos!" > out.txt        # перезаписать stdout в файл
echo "Hello caos!" >> out.txt       # добавить в конец файла
echo "Hello caos!" 2> err.txt       # перенаправить stderr в файл
echo "Hello caos!" 2>> err.txt      # добавить stderr в конец файла
```

Перенаправить stderr туда же, куда идёт stdout:

```bash
cmd > out.txt 2>&1
```

Порядок важен: сначала `>` перенаправляет stdout в файл, затем `2>&1` делает stderr дубликатом нового stdout. Если
написать наоборот (`2>&1 > out.txt`), stderr останется на терминале.

Подавить поток, направив его в «чёрную дыру»:

```bash
cmd > /dev/null      # подавить stdout
cmd 2> /dev/null     # подавить stderr
cmd > /dev/null 2>&1 # подавить оба потока
```

## Команда tee

`tee` читает stdin и одновременно копирует данные на stdout и в указанный файл:

```bash
cmd | tee out.txt        # перезаписать файл
cmd | tee -a out.txt     # добавить в конец файла
```

Это позволяет продолжить конвейер и при этом сохранить промежуточный результат. Например:

```bash
make 2>&1 | tee build.log | grep -i error
```

## Системные вызовы dup и dup2

Shell реализует все перенаправления через системные вызовы `dup`, `dup2` и `dup3`:

```c
#include <unistd.h>

int dup(int oldfd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
```

- `dup(oldfd)` создаёт новый дескриптор, ссылающийся на то же самое открытое файловое описание, что и `oldfd`, и
  возвращает наименьший свободный номер FD.
- `dup2(oldfd, newfd)` — если `newfd` уже открыт, сначала атомарно его закрывает, затем делает `newfd` дубликатом
  `oldfd`.
- `dup3` аналогичен `dup2`, но дополнительно принимает флаги (например `O_CLOEXEC`).

Дубликаты дескрипторов разделяют одно и то же открытое файловое описание: одну текущую позицию в файле, одни флаги
статуса. Изменение позиции через один дескриптор видно через другой.

Пример перенаправления stdout процесса в файл:

```c
#include <fcntl.h>
#include <unistd.h>

int fd = open("log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
dup2(fd, 1);    // теперь stdout (FD 1) пишет в log.txt
close(fd);      // оригинальный дескриптор больше не нужен
```

```
 До dup2(fd, 1):                  После dup2(fd, 1) + close(fd):

 fd[0] stdin  ──▶ terminal        fd[0] stdin  ──▶ terminal
 fd[1] stdout ──▶ terminal        fd[1] stdout ──▶ open file description
 fd[2] stderr ──▶ terminal        fd[2] stderr ──▶ terminal    │
 fd[3] fd     ──▶ open file       fd[3] (closed)               │
                  description ─┐                               │
                               │       ┌───────────────────────┘
                               │       ▼
                               └──▶ ┌─────────────────────────┐
                                    │ offset: 0               │
                                    │ flags:  O_WRONLY        │──▶ inode log.txt
                                    │ refcnt: 1               │
                                    └─────────────────────────┘
```

После `dup2` вызовы `printf` или `write(1, ...)` будут записывать данные в `log.txt`.

## Реализация tee

```c
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    char buf[4096];
    while (1) {
        ssize_t n = read(0, buf, sizeof(buf));   // stdin
        if (n == 0)
            break;
        if (n == -1) {
            perror("read");
            return 1;
        }

        ssize_t w1 = write(1, buf, n);           // stdout
        if (w1 == -1) {
            perror("write stdout");
            return 1;
        }

        ssize_t w2 = write(fd, buf, n);
        if (w2 == -1) {
            perror("write file");
            return 1;
        }
    }

    close(fd);
    return 0;
}
```

Для поддержки флага `-a` (добавление в конец) достаточно заменить `O_TRUNC` на `O_APPEND` при открытии файла.

## Как shell реализует конвейер

Оператор `|` в shell реализован через системный вызов `pipe`, который создаёт пару связанных дескрипторов: записанное в
один конец немедленно можно читать из другого:

```c
#include <unistd.h>

int pipe(int pipefd[2]);
// pipefd[0] — конец для чтения
// pipefd[1] — конец для записи
```

Shell создаёт канал, затем разветвляется (`fork`). В дочернем процессе (левая часть) `dup2(pipefd[1], 1)` перенаправляет
stdout в запись канала; в правой части `dup2(pipefd[0], 0)` перенаправляет stdin из чтения канала. После этого оба конца
закрываются в обоих процессах — все копии пишущего конца должны быть закрыты, иначе читатель никогда не получит EOF.

Шаги реализации `cmd1 | cmd2`:

```
 ШАГ 1: shell вызывает pipe()
 ┌───────────────────────────────────────────────┐
 │  shell                                        │
 │  fd[0] ──▶ [pipe read end ]  ◀──▶  kernel     │
 │  fd[1] ──▶ [pipe write end]         buffer    │
 └───────────────────────────────────────────────┘

 ШАГ 2: fork() — оба процесса наследуют fd[0] и fd[1]
 ┌─────────────────────┐      ┌─────────────────────┐
 │  child 1 (cmd1)     │      │  child 2 (cmd2)     │
 │  fd[0] (read)       │      │  fd[0] (read)       │
 │  fd[1] (write)      │      │  fd[1] (write)      │
 └─────────────────────┘      └─────────────────────┘

 ШАГ 3: dup2 + close в каждом child
 ┌─────────────────────────────┐  данные   ┌──────────────────────────┐
 │  child 1 (cmd1)             │ ────────▶ │  child 2 (cmd2)          │
 │                             │           │                          │
 │  dup2(fd[1], 1)             │           │  dup2(fd[0], 0)          │
 │  stdout (1) ──▶ pipe write  │           │  stdin  (0) ◀── pipe read│
 │  close(fd[0])  ✓            │           │  close(fd[1])  ✓         │
 │  close(fd[1])  ✓            │           │  close(fd[0])  ✓         │
 └─────────────────────────────┘           └──────────────────────────┘
        │                                           ▲
        │         ┌─────────────────────────────────────────────────────┐
        └────────▶│   kernel pipe buf                                   │───────────┘
                  │  (≈ 65536 байт default; до 1 MiB через F_SETPIPE_SZ)│
                  └─────────────────────────────────────────────────────┘

 Закрыть все копии fd[1] (write end) обязательно:
 пока хоть один write end открыт — читатель не получит EOF.
```

## Связанные темы

- [Файловые дескрипторы](file_descriptors.md) — `open`, `read`, `write` и структура FD
- [Открытые файлы и процессы](open_files_and_processes.md) — наследование дескрипторов при `fork`
- [Блочные и символьные устройства](block_and_character_devices.md) — `/dev/null` и `/dev/tty`

## Источники

- `man 2 dup` — дублирование файловых дескрипторов
- `man 2 dup2` — `dup` с указанием номера нового дескриптора
- `man 2 pipe` — создание канала
- `man 1 tee` — команда tee в оболочке
- `man 7 pipe` — механизм конвейеров
- Bash Reference Manual, раздел «Redirections»
