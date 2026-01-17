#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    cpu_set_t mask;
    sched_getaffinity(getpid(), sizeof(mask), &mask);

    if (CPU_ISSET(0, &mask)) {
        printf("Ядро 0 в affinity-маске\n");
    }
}