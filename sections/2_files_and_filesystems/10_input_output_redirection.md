##### Перенаправление ввода-вывода. Как в терминале направить вывод команды в файл с перезаписью файла? А без перезаписи, в режиме добавления в файл? Как перенаправить вывод одного из потоков (cout или cerr) в файл или в другой поток? Как подавить вывод какого-то из потоков?

**Перенаправление в shell (bash):**

- Перезаписать файл:
  ```bash
  echo "Hello caos!" > out.txt        # перенаправить stdout
  echo "Hello caos!" 2> err.txt       # перенаправить stderr
  ```
- Добавление в конец:
  ```bash
  echo "Hello caos!" >> out.txt
  echo "Hello caos!" 2>> err.txt
  ```
- Перенаправить stderr в тот же файл, что и stdout:
  ```bash
  echo "Hello caos!" > out.txt 2>&1
  ```
- Подавить поток:
  ```bash
  echo "Hello caos!" > /dev/null      # убрать stdout
  echo "Hello caos!" 2> /dev/null     # убрать stderr
  ```

`cout` обычно пишет в stdout (fd 1), `cerr` — в stderr (fd 2).

##### Что делает команда tee?

**tee в shell:**

```bash
cmd | tee out.txt
cmd | tee -a out.txt    # с добавлением
```

`tee` читает stdin и одновременно:

- копирует поток на stdout (чтобы дальше в конвейере его можно было использовать);
- пишет копию в файл.

##### Что делают сисколлы dup и dup2?

**dup / dup2 / dup3:**

```c
#include <unistd.h>

int dup(int oldfd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
```

- `dup(oldfd)` создаёт новый дескриптор, ссылающийся на **то же самое открытое файловое описание**, что и `oldfd`, и возвращает минимальный свободный номер FD.
- `dup2(oldfd, newfd)`:
  - если `newfd` уже открыт — сначала его закрывает;
  - затем делает `newfd` дубликатом `oldfd`.
- `dup3` то же самое, но дополнительно позволяет задать флаг `O_CLOEXEC`.

Shell реализует перенаправления `>`, `2>`, конвейеры и т.п. именно через комбинации `open` + `dup2`.

Пример перенаправления stdout процесса на файл:

```c
int fd = open("log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
dup2(fd, 1);    // теперь stdout (FD 1) пишет в log.txt
close(fd);
```

##### Реализуйте программу tee

```c
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
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

        // Пишем и в stdout, и в файл
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
}
```

Опционально можно добавить ключ `-a` для записи в режиме append (`O_APPEND` вместо `O_TRUNC`).
