#include <sched.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

/*
__vdso_clock_gettime
__vdso_getcpu
__vdso_gettimeofday
__vdso_time
*/

int main() {
    unsigned int cpu;
    unsigned int node;
    printf("%d\n", getcpu(&cpu, &node));
    printf("%u, %u\n", cpu, node);

    struct timeval tv;
    printf("%d\n", gettimeofday(&tv, NULL));

    time_t timer;
    printf("%ld\n", time(&timer));

    struct timespec t;
    printf("%d\n", clock_gettime(0, &t));
}