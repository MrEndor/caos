#include <stdio.h>

// объявить функцию из ассемблера
int is_prime(long n);

int main() {
    for (int i = 2; i <= 20; i++) {
        if (is_prime(i)) {
            printf("%d is prime\n", i);
        }
    }
    return 0;
}
