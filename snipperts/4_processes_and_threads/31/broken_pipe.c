#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

void handler(int sig) {
    printf("Got signal %d\n", sig);
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = 0;

    sigaction(SIGPIPE, &sa, NULL);

    int pipefd[2];
    pid_t pid;

    pipe(pipefd);
    pid = fork();

    if (pid == 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        exit(0);
    } else {
        wait(NULL);

        close(pipefd[0]);

        printf("broken pipe...\n");
        if (write(pipefd[1], "test", 4) == -1) {
            perror("error while write");
        }

        close(pipefd[1]);
    }
}