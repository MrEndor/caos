#include <errno.h>
#include <stdio.h>
#include <string.h>

int main() {
    ssize_t res = read(fd, buf, size);
    if (res == -1) {
        int err = errno;               // сохранить сразу, пока не потеряли
        fprintf(stderr, "read failed: %d (%s)\n", err, strerror(err));
    }
}