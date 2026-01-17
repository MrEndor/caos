#include <sys/resource.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    int nice_value = getpriority(PRIO_PROCESS, getpid());
    printf("Nice = %d\n", nice_value);
}