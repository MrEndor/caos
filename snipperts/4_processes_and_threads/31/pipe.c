#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

int main() {
    int fd[2]; // fd[0] = чтение, fd[1] = запись

    if (pipe(fd) == -1) {
        perror("pipe");
        return 1;
    }
    
    pid_t pid = fork();
    
    if (pid == 0) {
        close(fd[1]);
        
        char buffer[100];
        ssize_t n = read(fd[0], buffer, sizeof(buffer));
        
        if (n > 0) {
            printf("Child received: %.*s\n", (int)n, buffer);
        }
        
        close(fd[0]);
        return 0;
    } else {
        close(fd[0]);
        
        const char *msg = "Hello from parent!";
        write(fd[1], msg, strlen(msg));
        
        close(fd[1]);
        
        int status;
        waitpid(pid, &status, 0);
    }
    
    return 0;
}
