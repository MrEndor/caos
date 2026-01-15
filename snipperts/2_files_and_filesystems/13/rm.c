#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s file...\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (unlink(argv[i]) == -1) {
            perror(argv[i]);
        }
    }
}
