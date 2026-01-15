#include <stdio.h>
#include <stdint.h>

uint64_t rdtsc_ordered() {
    uint32_t lo, hi;
    asm volatile("xorl %%eax, %%eax\n\t"
                 "cpuid\n\t"
                 "rdtsc"
                 : "=a"(lo), "=d"(hi)
                 : 
                 : "rbx", "rcx");
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    uint64_t start = rdtsc_ordered();
    
    for (int i = 0; i < 1000000; i++) {
    }
    
    uint64_t end = rdtsc_ordered();
    
    printf("Elapsed cycles (ordered): %ld\n", end - start);
}
