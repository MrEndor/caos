#include <unistd.h>
#include <stdio.h>

int main() {
    pid_t mypid = getpid();
    pid_t parentpid = getppid();
    printf("PID=%d, PPID=%d\n", mypid, parentpid);
}