##### Что такое pipes?

**Pipe (конвейер)** -- это **однонаправленный канал связи** между двумя процессами. Данные, написанные в один конец,
читаются с другого.

Особенности:

- **однонаправленный** (если нужна двусторонняя связь -- два pipe'а);
- **буферизированный** (ограниченный размер буфера, обычно 64KB);
- **файловые дескрипторы** (трактуется как файл);
- **работает только между процессами с общим предком** (обычно родитель и дочерний).

##### Покажите в коде пример создания pipe и общения между двумя процессами с помощью pipe.

```c
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

int main() {
    int fd[2];  // fd[0] = чтение, fd[1] = запись

    if (pipe(fd) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();

    if (pid == 0) {
        // Дочерний процесс -- читатель
        close(fd[1]);           // закрыть конец для записи

        char buffer[100];
        ssize_t n = read(fd[0], buffer, sizeof(buffer));

        if (n > 0) {
            printf("Child received: %.*s\n", (int)n, buffer);
        }

        close(fd[0]);
        return 0;
    } else {
        // Родительский процесс -- писатель
        close(fd[0]);           // закрыть конец для чтения

        const char *msg = "Hello from parent!";
        write(fd[1], msg, strlen(msg));

        close(fd[1]);

        // Подождать дочернего
        int status;
        waitpid(pid, &status, 0);
    }
}
```

Вывод:

```
Child received: Hello from parent!
```

##### В какой ситуации возникает ошибка Broken pipe?

**Broken pipe** -- ошибка (`SIGPIPE` сигнал или `EPIPE` errno) возникает, когда:

1. процесс пишет в pipe, но **все читатели уже закрыли его**;
2. ядро не может доставить данные (нет получателя).

Пример:

```bash
# В shell
cat | head -n 1   # head закроет pipe после 1 строки
# cat получит SIGPIPE и завершится
```

В коде:

```c
// Родитель пишет, дочерний закрывает pipe
pipe(fd);
fork();

// В дочернем:
close(fd[1]);           // закрыть конец записи
exit(0);

// В родителе:
write(fd[1], ...);      // EPIPE или SIGPIPE здесь!
```

**Защита:**

```c
signal(SIGPIPE, SIG_IGN);      // игнорировать SIGPIPE

ssize_t n = write(fd[1], buf, size);
if (n == -1 && errno == EPIPE) {
    printf("Broken pipe\n");
}
```

##### Как реализовать аналог оператора | в bash на Си?

Команда `prog1 | prog2` запускает две программы, связав вывод `prog1` на вход `prog2` через pipe.

```c
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
    int fd[2];
    pipe(fd);

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // Первый процесс: prog1
        close(fd[0]);                   // закрыть чтение
        dup2(fd[1], 1);                 // перенаправить stdout в pipe
        close(fd[1]);

        execlp("echo", "echo", "Hello Caos", NULL);
        perror("execlp");
        return 1;
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Второй процесс: prog2
        close(fd[1]);                   // закрыть запись
        dup2(fd[0], 0);                 // перенаправить stdin в pipe
        close(fd[0]);

        execlp("wc", "wc", "-w", NULL);
        perror("execlp");
        return 1;
    }

    // Родитель закрывает оба конца
    close(fd[0]);
    close(fd[1]);

    // Подождать обоих детей
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}
```

Этот код эквивалентен `echo "Hello World" | wc -w`.

**Ключевые моменты:**

- `pipe()` создаёт два дескриптора;
- каждый дочерний процесс закрывает ненужный конец;
- `dup2()` перенаправляет файловый дескриптор (0=stdin, 1=stdout, 2=stderr);
- родитель закрывает оба конца после fork (они унаследованы дочерними).
