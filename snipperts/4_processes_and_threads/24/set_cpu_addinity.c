#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    getchar();

    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    CPU_SET(1, &mask);

    sched_setaffinity(getpid(), sizeof(mask), &mask);

    getchar();
}