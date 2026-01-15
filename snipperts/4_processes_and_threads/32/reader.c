#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *fifo_path = "/tmp/my_fifo";

//    unlink(fifo_path);
//    mkfifo(fifo_path, 0666);

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
