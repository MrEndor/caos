#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int fd = open("/dev/pts/1", O_WRONLY);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    dup2(fd, 1);
    // dup2(fd, 2);

    close(fd);

    printf("Hello from process!\n");
}
