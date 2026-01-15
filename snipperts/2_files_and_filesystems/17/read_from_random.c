#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int fd = open("/dev/random", O_RDONLY);
    unsigned char buf[32];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    printf("%s", (char*)buf);
}
