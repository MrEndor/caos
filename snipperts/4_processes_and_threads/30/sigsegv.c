#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <stdint.h>

static const char *error_msg = "SIGSEGV пойман! ";
static volatile sig_atomic_t segfault_count = 0;
static volatile sig_atomic_t recovery_attempted = 0;

void segfault_handler(int sig, siginfo_t *info, void *ucontext_ptr) {
    segfault_count++;

    write(STDERR_FILENO, error_msg, strlen(error_msg));

    char addr_buf[32];
    int len = snprintf(addr_buf, sizeof(addr_buf), "Адрес: %p, Код: %d\n",
                       info->si_addr, info->si_code);
    write(STDERR_FILENO, addr_buf, len);

    if (segfault_count == 1 && recovery_attempted == 0) {
        recovery_attempted = 1;

        void *fault_addr = info->si_addr;
        uintptr_t page_start = (uintptr_t)fault_addr & ~(4095ULL);  // 4KB alignment

        if (mprotect((void*)page_start, 4096, PROT_READ | PROT_WRITE) == 0) {

            const char *recovery_msg = "Восстановление прошло! Продолжаю...\n";
            write(STDERR_FILENO, recovery_msg, strlen(recovery_msg));
            return;
        }
    }

    const char *fatal_msg = "Восстановление не удалось. Завершаюсь.\n";
    write(STDERR_FILENO, fatal_msg, strlen(fatal_msg));
    _exit(1);
}

int main() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_sigaction = segfault_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("Запись в защищённую страницу\n");
    {
        void *page = mmap(NULL, 4096, PROT_READ,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) {
            perror("mmap");
            exit(1);
        }

        int *protected_ptr = (int*)page;
        *protected_ptr = 123;

        printf("Значение после восстановления: %d\n", *protected_ptr);

        munmap(page, 4096);
    }

    printf("Повторный segfault\n");
    {
        int *ptr = NULL;
        *ptr = 999;  // → SIGSEGV #3 → восстановление НЕ поможет
    }

    printf("Это не выполнится\n");
    return 0;
}
