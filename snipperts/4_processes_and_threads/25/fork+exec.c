#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
    pid_t child = fork();

    if (child == -1) {
        perror("error while fork");
        return 1;
    }

    if (child == 0) {
        execl("/bin/ls", "ls", "-la", "/tmp", NULL);

        perror("execl failed");
        return 1;
    }
    int status;
    waitpid(child, &status, 0);

    if (WIFEXITED(status)) {
        printf("Child exited with code %d\n", WEXITSTATUS(status));
    }
}