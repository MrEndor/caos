#include <unistd.h>
#include <errno.h>
#include <stdio.h>

int main() {
    if (setuid(1000) == -1) {
        perror("setuid failed");
    }

    if (chdir("/tmp") == -1) {
        perror("chdir failed");
    }

    getchar();

    seteuid(0);

    getchar();
}