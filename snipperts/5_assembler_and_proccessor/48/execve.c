#include <stdio.h>
#include <string.h>

// Выполнить: /bin/ls -la /tmp
void execve_demo() {
    const char *filename = "/bin/ls";
    const char *argv[] = {"/bin/ls", "-la", "/tmp", NULL};
    const char *envp[] = {NULL};
    
    asm volatile(
        "mov $59, %%rax\n\t"           // syscall 59 = execve
        "mov %0, %%rdi\n\t"            // arg1: filename
        "mov %1, %%rsi\n\t"            // arg2: argv (указатель на массив)
        "mov %2, %%rdx\n\t"            // arg3: envp (указатель на массив)
        "syscall"
        :
        : "r"(filename), "r"(argv), "r"(envp)
        : "rax", "rdi", "rsi", "rdx"
    );
    
    printf("execve failed\n");
}

int main() {
    execve_demo();
}
