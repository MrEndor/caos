#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    const char *shm_name = "/my_shared_mem";
    size_t size = 1024;

    int fd = shm_open(shm_name, O_RDONLY, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    printf("Read: %s\n", (char *)addr);

    munmap(addr, size);
    close(fd);
}