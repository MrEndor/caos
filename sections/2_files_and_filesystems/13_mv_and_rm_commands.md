##### Как работает и какие сисколлы использует программа mv?

Основная операция — системный вызов `rename`/`renameat`:

```c
#include <stdio.h>
#include <unistd.h>

int rename(const char *oldpath, const char *newpath);
```

Если **источник и назначение находятся в одной файловой системе**, `rename`:

- просто меняет запись в директории (имя или каталог);
- данные на диске не копируются.

Если файловые системы разные, классическая `mv` делает:

1. копирование содержимого (по сути `cp` в другое место);
2. `unlink` (удаление) исходного файла.

---

##### Как работает и какие сисколлы использует программа rm?

Для удаления файлов используется `unlink` / `unlinkat`:

```c
#include <unistd.h>

int unlink(const char *pathname);
```

`unlink`:

- удаляет **запись в каталоге** (имя → inode);
- уменьшает `st_nlink` у inode;
- когда `st_nlink` достигает 0 и нет открытых дескрипторов, данные могут быть освобождены.

Для директорий используется `rmdir` (удаляет пустой каталог).

---

##### Напишите упрощенную реализацию и того, и другого.

**Упрощённая реализация `mv` (одна ФС):**

```c
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    if (rename(argv[1], argv[2]) == -1) {
        perror("rename");
        return 1;
    }
}
```

---

**Упрощённая реализация `rm` для обычных файлов:**

```c
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s file...\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (unlink(argv[i]) == -1) {
            perror(argv[i]);
        }
    }
}
```