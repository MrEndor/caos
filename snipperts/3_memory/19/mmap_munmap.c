#include <sys/mman.h>
#include <stdio.h>

int main() {
    void *ptr = mmap(NULL, 4096,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    ((char *)ptr)[0] = 'A';

    munmap(ptr, 4096);
}
