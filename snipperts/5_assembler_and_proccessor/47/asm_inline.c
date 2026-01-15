#include <stdio.h>

int main() {
    int a = 5, b = 3, result;
    
    // Базовая синтаксис
    asm("movl %1, %%eax\n\t"
        "addl %2, %%eax\n\t"
        "movl %%eax, %0"
        : "=r" (result)                 // output constraints
        : "r" (a), "r" (b)              // input constraints
        :                                // clobber list
    );
    
    printf("result = %d\n", result);    // 8
    return 0;
}
