#include <stdio.h>

int main() {
    int a = 5, b = 10;
    
    printf("Before: a=%d, b=%d\n", a, b);
    
    asm("xchgl %0, %1"
        : "+r" (a), "+r" (b)
    );
    
    printf("After: a=%d, b=%d\n", a, b);
}
