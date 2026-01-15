#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
    int fd[2];
    pipe(fd);

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(fd[0]);                   // закрыть чтение
        dup2(fd[1], STDOUT_FILENO);                 // перенаправить stdout в pipe
        close(fd[1]);

        execlp("echo", "echo", "Hello World", NULL);
        perror("execlp");
        return 1;
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(fd[1]);                   // закрыть запись
        dup2(fd[0], STDIN_FILENO);                 // перенаправить stdin в pipe
        close(fd[0]);

        execlp("wc", "wc", "-w", NULL);
        perror("execlp");
        return 1;
    }

    close(fd[0]);
    close(fd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}
