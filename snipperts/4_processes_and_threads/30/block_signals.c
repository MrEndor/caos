#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

void handler(int sig) {
    sigset_t block_all, old_mask;

    sigfillset(&block_all);
    sigprocmask(SIG_BLOCK, &block_all, &old_mask);

    printf("Критическая секция: все сигналы заблокированы\n");
    sleep(5);

    printf("Сигналы разблокированы\n");
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
}

int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;

    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR2);
    sigaddset(&sa.sa_mask, SIGINT);

    sa.sa_flags = 0;

    sigaction(SIGUSR1, &sa, NULL);

    raise(SIGUSR1);
    sleep(0.5);
    raise(SIGUSR2);
}