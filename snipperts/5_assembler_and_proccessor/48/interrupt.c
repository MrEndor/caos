#include <stdio.h>

// int 3 -- breakpoint (вызовет SIGTRAP)
void trigger_breakpoint() {
    asm volatile("int $3");  // breakpoint
}

// int 0x80 -- системный вызов (старый x86-32)
void syscall_int80(int number, int arg1) {
    asm volatile(
        "mov %0, %%eax\n\t"     # номер syscall
        "mov %1, %%ebx\n\t"     # arg1
        "int $0x80"
        : : "r"(number), "r"(arg1)
        : "eax", "ebx"
    );
}

void fast_syscall() {
    asm volatile(
        "mov $1, %%rax\n\t"     # syscall 1 = write
        "mov $1, %%rdi\n\t"     # fd = stdout
        "mov %0, %%rsi\n\t"     # buffer
        "mov $5, %%rdx\n\t"     # size = 5
        "syscall"
        : : "r"("Hello")
        : "rax", "rdi", "rsi", "rdx"
    );
}
