#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    struct stat sb;

    if (stat("myfile.txt", &sb) == -1) {
        perror("stat");
        return 1;
    }

    printf("Current permissions: %o\n", sb.st_mode & 0777);
    printf("Owner UID: %d\n", sb.st_uid);

    if (chmod("myfile.txt", 0644) == -1) {
        perror("chmod");
        return 1;
    }

    if (chown("myfile.txt", 1001, 1002) == -1) {
        perror("chown");
        return 1;
    }
    
    return 0;
}