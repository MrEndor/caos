# Жёсткие и символические ссылки

## Жёсткая ссылка

Жёсткая ссылка (hard link) — это дополнительная запись в директории, указывающая на уже существующий inode. Все имена,
которые ссылаются на один inode, абсолютно равноправны. Счётчик `st_nlink` в inode хранит общее количество жёстких
ссылок.

Ограничения жёстких ссылок:

- нельзя создавать на директории (кроме служебных `.` и `..`);
- нельзя создавать между разными файловыми системами (inode уникален только в пределах одной ФС).

Создание в shell:

```bash
ln source hardlink
```

Пример: если создать жёсткую ссылку и затем удалить исходное имя, данные останутся доступны:

```bash
echo "hello" > file.txt
ln file.txt hardlink.txt
rm file.txt
cat hardlink.txt   # данные не удалены
```

## Символическая ссылка

Символическая ссылка (symlink) — отдельный объект файловой системы с собственным inode типа `l`. В данных этого inode
хранится строка-путь до целевого файла. При открытии симлинка ядро разыменовывает путь и продолжает обработку с реальным
файлом.

Создание в shell:

```bash
ln -s /path/to/target symlink_name
```

Преимущества перед жёсткими ссылками:

- можно создавать между разными файловыми системами;
- можно ссылаться на директории;
- можно ссылаться на несуществующие файлы (broken symlink — «битая» ссылка).

Недостатки:

- разыменование требует дополнительного обращения к диску;
- удаление целевого файла делает симлинк битым.

## Сравнение

```
 Жёсткая ссылка (hard link)          Символическая ссылка (symlink)

 директория /home/user/              директория /home/user/
 ┌───────────────────────────┐        ┌───────────────────────────┐
 │ "file.txt"  ──▶ inode 42  │        │ "file.txt"  ──▶ inode 42  │
 │ "link.txt"  ──▶ inode 42  │        │ "link.txt"  ──▶ inode 99  │
 └───────────────────────────┘        └───────────────────────────┘
          │            │                           │          │
          └────────────┘                           │          │
                │                                  ▼          ▼
                ▼                        ┌──────────────┐  ┌──────────────┐
       ┌──────────────────┐              │  inode 99    │  │  inode 42    │
       │    inode 42      │              │  type: l     │  │  type: -     │
       │    type: -       │              │  nlink: 1    │  │  nlink: 1    │
       │    nlink: 2  ←── │ два имени    │  data:       │  │  data:       │
       │    data:         │              │ "/home/user/ │  │  (содержимое)│
       │    (содержимое)  │              │   file.txt"  │  │              │
       └──────────────────┘              └──────┬───────┘  └──────────────┘
                                                │                  ▲
                                                │  разыменование   │
                                                └──────────────────┘
                                          ядро читает путь из data inode 99
                                          и открывает inode 42

 rm file.txt:                         rm file.txt:
 ┌───────────────────────────┐        ┌───────────────────────────┐
 │ "link.txt"  ──▶ inode 42  │        │ "link.txt"  ──▶ inode 99  │
 └───────────────────────────┘        └───────────────────────────┘
       nlink: 1, данные живы                inode 99 data: "/home/user/file.txt"
                                            → ENOENT: битая ссылка
```

| Свойство                    | Жёсткая ссылка | Символическая ссылка |
|-----------------------------|----------------|----------------------|
| Тип в `ls -l`               | `-` (как файл) | `l`                  |
| Указывает на                | inode          | строку-путь          |
| Между разными ФС            | Нет            | Да                   |
| На директории               | Нет            | Да                   |
| Выживает удаление оригинала | Да             | Нет (битая ссылка)   |
| Увеличивает `st_nlink`      | Да             | Нет                  |

## Системные вызовы link и symlink

```c
#include <unistd.h>

int link(const char *oldpath, const char *newpath);      // жёсткая ссылка
int symlink(const char *target, const char *linkpath);   // символическая ссылка
```

- `link` создаёт новую запись в директории, указывающую на тот же inode, что и `oldpath`;
- `symlink` создаёт новый inode типа `l`, содержащий строку `target`.

Прочитать цель символической ссылки (не разыменовывая её):

```c
#include <unistd.h>

ssize_t readlink(const char *path, char *buf, size_t bufsiz);
```

## Реализация ln

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
    const char *dst = argv[arg + 1];

    int res;
    if (symbolic) {
        res = symlink(src, dst);
    } else {
        res = link(src, dst);
    }

    if (res == -1) {
        perror("link/symlink");
        return 1;
    }
    return 0;
}
```

## Варианты с дескриптором директории

Для использования в многопоточных программах и при работе с дескрипторами директорий существуют `at`-варианты:

```c
#include <fcntl.h>
#include <unistd.h>

int linkat(int olddirfd, const char *oldpath,
           int newdirfd, const char *newpath, int flags);

int symlinkat(const char *target, int newdirfd, const char *linkpath);

ssize_t readlinkat(int dirfd, const char *pathname,
                   char *buf, size_t bufsiz);
```

Если вместо дескриптора директории передать `AT_FDCWD`, функция работает относительно текущего рабочего каталога, как
обычный вариант.

## Связанные темы

- [Основы файловых систем](filesystems_basics.md) — inode и механизм именования файлов
- [Команды mv и rm](mv_and_rm_commands.md) — `unlink`, счётчик `st_nlink`
- [Операции с директориями](directory_operations.md) — `lstat` для проверки типа объекта
- [Права доступа и атрибуты файлов](file_permissions_and_attributes.md) — права на симлинки

## Источники

- `man 2 link` — создание жёсткой ссылки
- `man 2 symlink` — создание символической ссылки
- `man 2 readlink` — чтение пути из символической ссылки
- `man 2 unlink` — удаление записи из директории
- `man 2 linkat` — вариант link с дескрипторами директорий
- `man 1 ln` — команда ln в оболочке