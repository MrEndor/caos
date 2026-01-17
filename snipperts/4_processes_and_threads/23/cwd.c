#include <unistd.h>
#include <stdio.h>

int main() {
    printf("UID=%d, EUID=%d\n", getuid(), geteuid());

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("CWD=%s\n", cwd);
    }

    return 0;
}