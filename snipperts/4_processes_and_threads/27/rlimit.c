#include <sys/resource.h>
#include <stdio.h>

int main() {
    struct rlimit lim;

    getrlimit(RLIMIT_STACK, &lim);
    printf("Текущий soft limit: %ld bytes\n", lim.rlim_cur);
    printf("Hard limit: %ld bytes\n", lim.rlim_max);

    lim.rlim_cur = 128 * 1024 * 1024;

    if (setrlimit(RLIMIT_STACK, &lim) == 0) {
        printf("Successfully increased stack size\n");
    } else {
        perror("setrlimit failed");
    }

    getrlimit(RLIMIT_STACK, &lim);
    printf("New soft limit: %ld bytes\n", lim.rlim_cur);
}