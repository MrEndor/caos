##### Что такое fifo-файлы?

**FIFO (First In, First Out)** -- это именованный pipe, который выглядит как обычный файл в файловой системе, но
используется для inter-process communication.

Особенности:

- имеет **имя в файловой системе** (в отличие от обычного pipe);
- могут общаться **процессы без отношения родитель-дочерний**;
- это специальный файл (тип 'p');
- данные **не сохраняются** на диск (живут в буфере памяти).

##### Как создать такой файл из терминала, а также программно?

**Из терминала:**

```bash
mkfifo /tmp/my_fifo         # создать FIFO файл
ls -la /tmp/my_fifo         # покажет: prw-rw-rw-
rm /tmp/my_fifo             # удалить
```

**Программно:**

```c
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    const char *fifo_path = "/tmp/my_fifo";

    // Удалить старый файл, если существует
    unlink(fifo_path);

    // Создать FIFO
    if (mkfifo(fifo_path, 0666) == -1) {
        perror("mkfifo");
        return 1;
    }

    printf("FIFO создан: %s\n", fifo_path);

    // Можно использовать как обычный файл
    // ...

    unlink(fifo_path);
}
```

##### Покажите пример общения между двумя процессами с помощью fifo-файла.

**Процесс 1 (писатель):**

```c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *fifo_path = "/tmp/my_fifo";

    // Открыть FIFO для записи
    int fd = open(fifo_path, O_WRONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    const char *message = "Hello from writer!";
    write(fd, message, strlen(message));

    close(fd);
    printf("Message sent\n");
}
```

**Процесс 2 (читатель):**

```c
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *fifo_path = "/tmp/my_fifo";

    // Создать FIFO, если не существует
    unlink(fifo_path);
    mkfifo(fifo_path, 0666);

    // Открыть FIFO для чтения (блокирующий вызов)
    int fd = open(fifo_path, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    char buffer[100];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);

    if (n > 0) {
        buffer[n] = '\0';
        printf("Received: %s\n", buffer);
    }

    close(fd);
    unlink(fifo_path);
}
```

**Использование:**

```bash
# Запустить читатель в background
./reader &

# Запустить писатель (заблокируется, пока нет читателя)
./writer
# Вывод: Message sent
```

##### Что, если несколько процессов пишут в один и тот же fifo?

Данные из всех писателей попадают в единый буфер FIFO и читаются в порядке поступления (FIFO). **Записи не
перемешиваются**, если:

- размер одной "порции" < 512 байт (гарантия атомарности на большинстве систем);
- иначе нужна синхронизация.

```c
// Несколько писателей
./writer1 &     # "Message 1"
./writer2 &     # "Message 2"
./reader        # Прочитает: "Message 1Message 2" или в другом порядке
```

##### Что, если несколько процессов читают один и тот же fifo?

Каждый `read()` вызов читает данные, и они **удаляются из буфера**. Если два процесса читают одновременно:

- один процесс может получить часть данных, другой -- оставшуюся часть;
- не все процессы получат одинаковые данные.

Это может быть **проблемой**, если нужна трансляция одних данных нескольким получателям -- лучше использовать несколько
FIFO или другой механизм (broadcast через сокеты, message queues и т.д.).

```c
// Два читателя
./reader1 &     # может прочитать "Hello "
./reader2 &     # может прочитать "World"
./writer        # пишет "Hello World"
```
