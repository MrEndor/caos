##### Жесткие и символические ссылки. В чем разница? Как создать жесткую ссылку, символическую ссылку, что они представляют из себя с точки зрения файловой системы?

**Жёсткая ссылка (hard link):**

- это **дополнительная запись в директории**, указывающая на тот же inode;
- все жёсткие ссылки на один файл **равноправны** -- нет "главной" и "вторичной";
- счётчик `st_nlink` в inode показывает количество жёстких ссылок;
- жёсткие ссылки нельзя делать:
    - на директории (кроме служебных `.` и `..`);
    - между разными файловыми системами.

Создание в shell:

```bash
ln source hardlink
```

**Символическая ссылка (symlink):**

- отдельный inode типа `l` (link);
- в его данных хранится **строка‑путь** до целевого файла;
- при открытии симлинка ядро разыменовывает путь (как если бы вы написали это имя в командной строке);
- можно создавать между разными ФС, на директории и даже на несуществующие файлы (broken symlink).

Создание:

```bash
ln -s target symlink
```

##### Как работает и какие сисколлы использует программа ln в случае создания жестких ссылок и символических ссылок?

```c
#include <unistd.h>

int link(const char *oldpath, const char *newpath);      // hard link
int symlink(const char *target, const char *linkpath);   // symbolic
```

`ln` при создании жёсткой ссылки использует `link`, при создании символической — `symlink`.

##### Напишите упрощенную реализацию

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int symbolic = 0;
    int arg = 1;

    if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
        symbolic = 1;
        arg = 2;
    }

    if (argc - arg != 2) {
        fprintf(stderr, "Usage: %s [-s] src dst\n", argv[0]);
        return 1;
    }

    const char *src = argv[arg];
    const char *dst = argv[arg+1];

    int res;
    if (symbolic) {
        res = symlink(src, dst);
    } else {
        res = link(src, dst);
    }

    if (res == -1) {
        perror("link");
        return 1;
    }
}
```