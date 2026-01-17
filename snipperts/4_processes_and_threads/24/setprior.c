#include <sys/resource.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    setpriority(PRIO_PROCESS, getpid(), 10);

    int nice_value = getpriority(PRIO_PROCESS, getpid());
    printf("Nice = %d\n", nice_value);
}
