#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

const size_t kPageSize = 4096;
const size_t kMapSize = kPageSize * 10;

int main() {
    void* addr = mmap(NULL, kMapSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        perror("mmap");
        return;
    }

    printf("Mapped at: %p, size: %zu bytes\n", addr, kMapSize);

    char* data = (char*)addr;
    strcpy(data, "Hello from mmap!");
    printf("Written: %s\n", data);

    if (munmap(addr, kMapSize) == -1) {
        perror("munmap");
    }

    printf("Unmapped\n\n");
}
