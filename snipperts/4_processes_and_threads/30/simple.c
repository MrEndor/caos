#include <signal.h>
#include <stdio.h>

void my_handler(int sig, siginfo_t *info, void *context) {
    printf("Сигнал %d от процесса %d\n", sig, info->si_pid);
}

int main() {
    struct sigaction sa;
    sa.sa_sigaction = my_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGUSR1, &sa, NULL);

    pause();
    return 0;
}