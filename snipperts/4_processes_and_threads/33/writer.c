#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *shm_name = "/my_shared_mem_1";
    size_t size = 1024;

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        return 1;
    }

    getchar();

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    const char *message = "Hello from shared memory!";
    strcpy((char *)addr, message);

    printf("Written: %s\n", (char *)addr);

    sleep(100);

    munmap(addr, size);
    close(fd);

    shm_unlink(shm_name);
}