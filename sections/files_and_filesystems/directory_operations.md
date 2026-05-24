# Операции с директориями

## Структура директории

Директория — это файл особого типа. Её данные — список пар (имя → номер inode), называемых directory entries (dentry).
Ядро хранит dentry в кеше (dcache) для ускорения path resolution:

```
 VFS dcache (в памяти)                         inode table (диск/кеш)
 ┌───────────────────────────────────────┐
 │  dentry "/"            inode 2        │──▶ ┌─────────────────┐
 │    └── dentry "home"   inode 10       │──▶ │  inode 10       │
 │          └── dentry "user"  inode 25  │──▶ │  type: dir      │
 │                ├── dentry "file.txt"  │    │  nlink: 2       │
 │                │         inode 42     │──▶ │                 │
 │                └── dentry "src"       │    └─────────────────┘
 │                          inode 30     │──▶ ┌─────────────────┐
 └───────────────────────────────────────┘    │  inode 42       │
                                              │  type: -        │
 Данные директории "user" (на диске):         │  nlink: 1       │
 ┌──────────────────────────────────────┐     │  size: 1024     │
 │ d_ino  │ d_reclen │ d_type │ d_name  │     └─────────────────┘
 ├────────┼──────────┼────────┼─────────┤
 │   25   │    12    │ DT_DIR │  "."    │  (сама директория)
 │   10   │    12    │ DT_DIR │  ".."   │  (родитель)
 │   42   │    16    │ DT_REG │"file.txt│
 │   30   │    12    │ DT_DIR │  "src"  │
 └──────────────────────────────────────┘
```

`readdir` возвращает записи именно из этого массива. Поле `d_type` может быть `DT_UNKNOWN` на некоторых ФС — тогда тип
проверяют через `stat`.

## Обход директории: opendir, readdir, closedir

Для перебора содержимого директории в C используется семейство функций из `<dirent.h>`:

```c
#include <dirent.h>

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
```

- `opendir(path)` открывает директорию и возвращает дескриптор типа `DIR*`;
- `readdir(dir)` возвращает указатель на `struct dirent` со следующей записью, или `NULL`, если записи закончились;
- `closedir(dir)` закрывает директорию и освобождает ресурсы.

Структура `struct dirent` содержит, среди прочего:

| Поле     | Описание                                                            |
|----------|---------------------------------------------------------------------|
| `d_name` | Имя элемента (строка)                                               |
| `d_ino`  | Номер inode                                                         |
| `d_type` | Тип объекта (`DT_REG`, `DT_DIR`, `DT_LNK` и др.), не всегда надёжен |

Поле `d_type` может быть `DT_UNKNOWN` на некоторых файловых системах — в этом случае тип нужно определять через `stat`.

## Получение метаданных: stat и её варианты

Системный вызов `stat` и его варианты заполняют структуру `struct stat` метаданными файла:

```c
#include <sys/stat.h>
#include <unistd.h>

int stat(const char *pathname, struct stat *buf);
int lstat(const char *pathname, struct stat *buf);
int fstat(int fd, struct stat *buf);
int fstatat(int dirfd, const char *pathname,
            struct stat *buf, int flags);
```

Структура `struct stat` включает:

```c
struct stat {
    ino_t     st_ino;    // номер inode
    mode_t    st_mode;   // тип файла и права доступа
    nlink_t   st_nlink;  // число жёстких ссылок
    uid_t     st_uid;    // UID владельца
    gid_t     st_gid;    // GID группы
    off_t     st_size;   // размер в байтах
    time_t    st_atime;  // время последнего доступа
    time_t    st_mtime;  // время последней модификации
    time_t    st_ctime;  // время последнего изменения метаданных
};
```

Отличия между вариантами:

| Функция   | Описание                                                                                      |
|-----------|-----------------------------------------------------------------------------------------------|
| `stat`    | По пути, разыменовывает симлинки                                                              |
| `lstat`   | По пути, не разыменовывает симлинки (данные о самой ссылке)                                   |
| `fstat`   | По открытому файловому дескриптору                                                            |
| `fstatat` | По пути относительно дескриптора директории; поддерживает флаги (`AT_SYMLINK_NOFOLLOW` и др.) |

Для проверки типа файла используются макросы:

```c
S_ISREG(st.st_mode)   // обычный файл
S_ISDIR(st.st_mode)   // директория
S_ISLNK(st.st_mode)   // символическая ссылка
S_ISBLK(st.st_mode)   // блочное устройство
S_ISCHR(st.st_mode)   // символьное устройство
```

## Реализация ls на C

Следующий пример демонстрирует простую реализацию `ls`, выводящую имя и размер каждого файла в директории:

```c
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";

    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;
    char full[4096];

    while ((entry = readdir(dir)) != NULL) {
        // Пропускаем . и ..
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
    return 0;
}
```

## Создание и удаление директорий

```c
#include <sys/stat.h>
#include <unistd.h>

int mkdir(const char *pathname, mode_t mode);
int rmdir(const char *pathname);
```

- `mkdir` создаёт директорию с указанными правами (результирующие права корректируются через `umask`);
- `rmdir` удаляет только пустую директорию — если в ней есть файлы, вернёт `ENOTEMPTY`.

Для рекурсивного удаления дерева директорий придётся самостоятельно обходить содержимое (через `opendir`/`readdir`) и
вызывать `unlink` или `rmdir` для каждого элемента.

## Связанные темы

- [Основы файловых систем](filesystems_basics.md) — inode и структура директорий
- [Команды mv и rm](mv_and_rm_commands.md) — `rename`, `unlink`, `rmdir`
- [Файловые дескрипторы](file_descriptors.md) — `open`, `read`, `fstat`
- [Жёсткие и символические ссылки](hard_and_symbolic_links.md) — разница между `stat` и `lstat`
- [Права доступа и атрибуты файлов](file_permissions_and_attributes.md) — биты `st_mode`, макросы `S_ISREG` и т.д.

## Источники

- `man 3 opendir` — открытие директории
- `man 3 readdir` — чтение записей директории
- `man 2 stat` — системный вызов stat
- `man 2 fstatat` — вариант stat с дескриптором директории
- `man 2 mkdir` — создание директории
- `man 2 rmdir` — удаление пустой директории
- `man 0p dirent.h` — структура dirent в POSIX
