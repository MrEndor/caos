#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        return 1;
    }

    int flags = O_WRONLY | O_CREAT;
    const char* filepath = argv[1];
    if (argc == 3 && strcmp(argv[1], "-a") == 0) {
        flags |= O_APPEND;
        filepath = argv[2];
    } else {
        flags |= O_TRUNC;
    }

    int fd = open(filepath, flags, 0644);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    char buf[4096];
    while (1) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n == 0)
            break;
        if (n == -1) {
            perror("read");
            return 1;
        }

        ssize_t w1 = write(1, buf, n);
        if (w1 == -1) {
            perror("write stdout");
            return 1;
        }

        ssize_t w2 = write(fd, buf, n);
        if (w2 == -1) {
            perror("write file");
            return 1;
        }
    }

    close(fd);
}
