#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s src dst\n", argv[0]);
        return 1;
    }

    if (rename(argv[1], argv[2]) == -1) {
        perror("rename");
        return 1;
    }
}
