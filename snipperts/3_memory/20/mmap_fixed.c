#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main() {
    printf("Before mmap\n");
    getchar();

    char* str = (char*)(mmap((void*)0x600000000000, 128 * 1024 * 1024,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0));

    if (str == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    printf("Mmapped\n");
    getchar();

    strcpy(str, "Hello, mmap!");
    printf("Say it: %s\n", str);

    if (munmap(str, 128 * 1024 * 1024) == -1) {
        perror("munmap");
        return 1;
    }
}