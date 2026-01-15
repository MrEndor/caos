##### Как из терминала посмотреть, какие файловые дескрипторы сейчас открыты у данного процесса и какие файлы им соответствуют?

В Linux для каждого процесса есть каталог `/proc/<pid>/fd`, в котором:

- каждое имя — номер дескриптора (`0`, `1`, `2`, …);
- каждая запись — **символическая ссылка** на реальный объект (`/dev/pts/0`, `socket:[12345]`, `pipe:[67890]`, обычный
  файл и т.п.).

Посмотреть для текущего процесса:

```bash
ls -l /proc/<pid>/fd
```

##### Как из терминала посмотреть, какие процессы сейчас держат открытым данный файл?

Инструменты:

- `lsof` (List Open Files):

```bash
lsof path/to/file
lsof -p <pid>
```

- `fuser`:

```bash
fuser path/to/file
```

##### Какие команды есть в терминале для этого и как эти команды реализовать на языке Си?

```c
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

int main() {
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

        printf("%s -> %s\n", entry->d_name, target);
    }

    closedir(dir);
}
```