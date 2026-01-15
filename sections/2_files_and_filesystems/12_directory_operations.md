##### Как пользоваться функциями opendir, readdir?

**opendir / readdir / closedir:**

```c
#include <dirent.h>

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
```

- `opendir(path)` возвращает указатель `DIR*` на открытый каталог;
- `readdir(dir)` возвращает указатель на структуру `struct dirent` с полями:
    - `d_name` — имя элемента;
    - `d_ino` — inode;
    - `d_type` — тип объекта (`DT_REG`, `DT_DIR`, `DT_LNK` и т.д., но не всегда надёжно);
- когда файлы кончились, `readdir` возвращает `NULL`;
- `closedir(dir)` закрывает каталог.

##### Как пользоваться сисколлами stat, fstat, lstat, fstatat?

**stat / lstat / fstat / fstatat:**

```c
#include <sys/stat.h>
#include <unistd.h>

int stat(const char *pathname, struct stat *buf);
int lstat(const char *pathname, struct stat *buf);
int fstat(int fd, struct stat *buf);
int fstatat(int dirfd, const char *pathname,
            struct stat *buf, int flags);
```

Все они заполняют структуру `struct stat`:

- размер (`st_size`);
- тип и права (`st_mode`);
- число жёстких ссылок (`st_nlink`);
- владелец (`st_uid`, `st_gid`);
- временные метки (`st_atime`, `st_mtime`, `st_ctime`);
- inode (`st_ino`) и т.п.

Отличия:

- `stat` — по пути, разыменовывая симлинки;
- `lstat` — **не** разыменовывает симлинки, возвращает информацию именно о самой ссылке;
- `fstat` — по файловому дескриптору;
- `fstatat` — путь относительно `dirfd`, с флагами (`AT_SYMLINK_NOFOLLOW` и т.д.).

##### Как на Си реализовать программу ls с помощью всего этого?

```c
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";

    DIR *dir = opendir(path);
    if (!dir) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;
    char full[4096];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full, &st) == -1) {
            perror("stat");
            continue;
        }

        printf("%-20s %10ld bytes\n",
               entry->d_name, (long)st.st_size);
    }

    closedir(dir);
}
```
