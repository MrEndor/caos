#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <cstring>

int main() {
    char buf[4096];

    while (1) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n == 0)
            break;
        if (n == -1) {
            int err = errno;
            fprintf(stderr, "read failed: %d (%s)\n", err, strerror(err));
            return 1;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(1, buf + written, n - written);
            if (m == -1) {
                perror("write");
                return 1;
            }
            written += m;
        }
    }
}