#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

const size_t kInitialSize = 4096;
const size_t kNewSize = 8192;

int main() {
    void* addr = mmap(NULL, kInitialSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    printf("Initial mapping: %p, size: %zu\n", addr, kInitialSize);

    char* data = (char*)addr;
    strcpy(data, "Hello mremap!");
    printf("Data: %s\n\n", data);

    printf("Resizing to %zu bytes...\n", kNewSize);

    void* new_addr = mremap(addr, kInitialSize, kNewSize, MREMAP_MAYMOVE);

    if (new_addr == MAP_FAILED) {
        perror("mremap");
        munmap(addr, kInitialSize);
        return 1;
    }

    printf("New mapping: %p, size: %zu\n", new_addr, kNewSize);
    printf("Data after resize: %s\n\n", (char*)new_addr);

    strcpy((char*)new_addr + 1000, "Added data after resize");
    printf("New data at offset 1000: %s\n\n", (char*)new_addr + 1000);

    if (new_addr != addr) {
        printf("Address changed (MREMAP_MAYMOVE)\n");
    } else {
        printf("Address unchanged\n");
    }

    munmap(new_addr, kNewSize);
    printf("\n");
}
