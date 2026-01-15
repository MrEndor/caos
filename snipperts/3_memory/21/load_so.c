#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    fstat(fd, &st);

    void *addr = mmap(NULL, st.st_size,
                      PROT_READ | PROT_EXEC,
                      MAP_PRIVATE,
                      fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    double (*func)(double) = (double (*)(double))(addr + 0x40);
    double res = func(2.0);
    printf("%f\n", res);

    munmap(addr, st.st_size);
    close(fd);
    return 0;
}
