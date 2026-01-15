#include <stdio.h>
#include <stdint.h>

uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    uint64_t start = rdtsc();
    
    for (int i = 0; i < 1000; i++) {
    }
        
    uint64_t end = rdtsc();
    
    printf("Elapsed cycles: %ld\n", end - start);
}
