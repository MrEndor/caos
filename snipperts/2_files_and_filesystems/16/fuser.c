#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

int main() {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;
    char path[256];
    char target[4096];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);

        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len == -1) {
            perror("readlink");
            continue;
        }
        target[len] = '\0';

        printf("%s -> %s\n", entry->d_name, target);
    }

    closedir(dir);
}