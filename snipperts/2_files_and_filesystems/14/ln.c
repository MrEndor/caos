#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int symbolic = 0;
    int arg = 1;

    if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
        symbolic = 1;
        arg = 2;
    }

    if (argc - arg != 2) {
        fprintf(stderr, "Usage: %s [-s] src dst\n", argv[0]);
        return 1;
    }

    const char *src = argv[arg];
    const char *dst = argv[arg+1];

    int res;
    if (symbolic) {
        res = symlink(src, dst);
    } else {
        res = link(src, dst);
    }

    if (res == -1) {
        perror("link");
        return 1;
    }
}
