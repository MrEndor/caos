#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    int in  = open(argv[1], O_RDONLY);
    if (in == -1) {
        perror("open src");
        return 1;
    }

    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out == -1) {
        perror("open dst");
        close(in);
        return 1;
    }

    char buf[4096];
    while (1) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n == 0)              // EOF
            break;
        if (n == -1) {
            perror("read");
            return 1;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(out, buf + written, n - written);
            if (m == -1) {
                perror("write");
                return 1;
            }
            written += m;
        }
    }

    close(in);
    close(out);
}
