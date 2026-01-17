#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main() {
    int fd = open("example.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    const char* text = "Hello, mmap file mapping!";
    write(fd, text, strlen(text));

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("fstat");
        close(fd);
        return 1;
    }

    char* data =
        (char*)mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    printf("File content: %.*s\n", (int)st.st_size, data);

    printf("Modifying first byte...\n");
    data[0] = 'X';

    if (msync(data, st.st_size, MS_SYNC) == -1) {
        perror("msync");
    }

    printf("Modified: %.*s\n\n", (int)st.st_size, data);

    munmap(data, st.st_size);
    close(fd);
}
