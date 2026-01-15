#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *fifo_path = "/tmp/my_fifo";

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
