##### Что такое файловый дескриптор?

**Файловый дескриптор (file descriptor, FD)** -- это неотрицательное число, которое является идентификатором потока
ввода-вывода. Дескриптор может быть связан с файлом, каталогом, сокетом.

##### Расскажите про сисколлы open, close и lseek для работы с файлами.

**open**

```c
#include <fcntl.h>

int open(const char *pathname, int flags, mode_t mode);
```

- `pathname` -- путь к файлу;
- `flags` -- флаги открытия:
    - `O_RDONLY`, `O_WRONLY`, `O_RDWR` -- режим: только чтение / только запись / чтение–запись;
    - `O_CREAT` -- создать файл, если его нет (тогда обязателен `mode`);
    - `O_TRUNC` -- обрезать существующий файл до нулевой длины;
    - `O_APPEND` -- все записи идут только в конец;
    - `O_NONBLOCK` -- неблокирующий режим и т.д.
- `mode` -- права доступа (например `0644`), используются только при создании (`O_CREAT`).

Возвращает **новый файловый дескриптор** или `-1` при ошибке (с установкой `errno`).

**close**

```c
#include <unistd.h>

int close(int fd);
```

Освобождает файловый дескриптор и связанные с ним ресурсы (уменьшает счётчик ссылок на «открытое файловое описание»). Возвращает 0 при успехе или `-1` при ошибке.

---

**lseek**

```c
#include <unistd.h>

off_t lseek(int fd, off_t offset, int whence);
```

Перемещает **текущую позицию** чтения/записи в открытом файле.

- `whence`:
    - `SEEK_SET` -- от начала файла;
    - `SEEK_CUR` -- от текущей позиции;
    - `SEEK_END` -- от конца файла.
- Возвращает новую позицию в байтах от начала файла, либо `-1` при ошибке.


##### Реализуйте программу cp с помощью данных сисколлов, а также read и write.

**Реализация `cp` через `open`, `read`, `write`, `close`:**

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    int in  = open(argv[1], O_RDONLY);
    if (in == -1) {
        perror("open src");
        return 1;
    }

    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out == -1) {
        perror("open dst");
        close(in);
        return 1;
    }

    char buf[4096];
    while (1) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0)              // EOF
            break;
        if (n == -1) {
            perror("read");
            return 1;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(out, buf + written, n - written);
            if (m == -1) {
                perror("write");
                return 1;
            }
            written += m;
        }
    }

    close(in);
    close(out);
    return 0;
}
```

Здесь `lseek` не используется, но его можно применять, например, чтобы переписать кусок файла «в середине».

##### Что произойдет, если сделать lseek на позицию больше чем размер файла и записать туда что-либо?

**Что будет, если сделать `lseek` за конец файла и что‑то записать:**

Если:

```c
int fd = open("file", O_WRONLY | O_CREAT, 0644);
lseek(fd, 1'000'000, SEEK_SET);
write(fd, "X", 1);
```

то:

- размер файла станет как минимум `1_000_001` байт;
- область между старым концом файла и позицией записи превратится в **«дыру»** (hole) — диапазон логических нулей, который на современных файловых системах (ext4, xfs и т.д.) **физически не занимает места на диске** (такой файл называется **sparse file**).

Проверка:

```bash
ls -lh file   # логический размер
du -h file    # реально занятое место
```
