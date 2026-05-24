# Открытые файлы и процессы

## /proc/PID/fd — дескрипторы процесса

В Linux для каждого процесса существует каталог `/proc/<pid>/fd`. Каждый элемент в нём:

- называется по номеру дескриптора (`0`, `1`, `2`, …);
- является символической ссылкой на реальный объект: обычный файл, `/dev/pts/0`, `socket:[12345]`, `pipe:[67890]` и т.п.

Посмотреть дескрипторы конкретного процесса:

```bash
ls -l /proc/<pid>/fd
```

Для текущего процесса (самоссылка):

```bash
ls -l /proc/self/fd
```

Помимо `/proc/<pid>/fd`, полезны и соседние файлы:

- `/proc/<pid>/fdinfo/<n>` — подробная информация о дескрипторе (позиция, флаги);
- `/proc/<pid>/maps` — карта виртуальной памяти процесса.

## Инструменты: lsof и fuser

`lsof` (List Open Files) — мощный инструмент для просмотра открытых файлов:

```bash
lsof /path/to/file       # какие процессы держат открытым этот файл
lsof -p <pid>            # все открытые файлы процесса
lsof -u alice            # все открытые файлы пользователя alice
lsof -i :8080            # процессы, использующие порт 8080
```

`fuser` показывает PID процессов, использующих файл или сокет:

```bash
fuser /path/to/file      # PID процессов, использующих файл
fuser -k /path/to/file   # послать SIGKILL этим процессам
```

Оба инструмента читают информацию из `/proc`.

## Реализация просмотра дескрипторов на C

Следующий пример перебирает `/proc/self/fd` и выводит, на что указывает каждый дескриптор текущего процесса:

```c
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;
    char path[256];
    char target[4096];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);

        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len == -1) {
            perror("readlink");
            continue;
        }
        target[len] = '\0';

        printf("fd %s -> %s\n", entry->d_name, target);
    }

    closedir(dir);
    return 0;
}
```

Для другого процесса достаточно заменить `self` на его PID: `"/proc/%d/fd"`.

## Наследование дескрипторов при fork и exec

После `fork` дочерний процесс получает копию таблицы дескрипторов родителя: каждый дескриптор указывает на то же
открытое файловое описание (с той же позицией). Это означает:

- родитель и дочерний процесс разделяют смещение в файле — запись одного видна другому;
- закрытие дескриптора в одном процессе не влияет на другой.

При `exec` дескрипторы по умолчанию сохраняются, кроме тех, у которых установлен флаг `FD_CLOEXEC` (он же `O_CLOEXEC`
при открытии). Установить флаг после открытия:

```c
#include <fcntl.h>

int flags = fcntl(fd, F_GETFD);
fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
```

Это важно для безопасности: дескриптор с конфиденциальными данными не должен передаваться дочернему процессу через
`exec`.

## Связанные темы

- [Файловые дескрипторы](file_descriptors.md) — `open`, `read`, `write`, структура FD
- [Перенаправление ввода/вывода](input_output_redirection.md) — `dup2`, перенаправление stdin/stdout
- [Основы файловых систем](filesystems_basics.md) — `/proc`, `procfs`
- [Процессы: основы](../processes/processes_basics.md) — структура процесса, PCB
- [fork и exec](../processes/fork_and_exec.md) — наследование дескрипторов при создании процессов

## Источники

- `man 5 proc` — описание виртуальной файловой системы /proc
- `man 8 lsof` — команда lsof
- `man 1 fuser` — команда fuser
- `man 2 readlink` — чтение цели символической ссылки
- `man 2 fcntl` — управление свойствами дескриптора (F_GETFD, F_SETFD, FD_CLOEXEC)