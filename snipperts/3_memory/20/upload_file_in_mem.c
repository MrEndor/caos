#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    int fd = open("example.txt", O_RDWR);
    struct stat st;
    fstat(fd, &st);

    char *data = mmap(NULL, st.st_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    data[0] = 'X';

    msync(data, st.st_size, MS_SYNC);

    munmap(data, st.st_size);
    close(fd);
}
