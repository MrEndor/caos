#include <unistd.h>
#include <sys/syscall.h>

// Вызов exit(0) напрямую через syscall
void exit_syscall() {
    syscall(SYS_exit, 0);
    // или напрямую через ассемблер:
    asm volatile("movq $60, %%rax\n\t"      // syscall 60 = exit
                 "movq $0, %%rdi\n\t"       // arg1: exit code
                 "syscall"
                 : : : "rax", "rdi");
}
