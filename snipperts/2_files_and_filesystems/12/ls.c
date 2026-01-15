#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

void print_permissions(mode_t mode) {
    printf((S_ISDIR(mode)) ? "d" : "-");
    printf((mode & S_IRUSR) ? "r" : "-");
    printf((mode & S_IWUSR) ? "w" : "-");
    printf((mode & S_IXUSR) ? "x" : "-");
    printf((mode & S_IRGRP) ? "r" : "-");
    printf((mode & S_IWGRP) ? "w" : "-");
    printf((mode & S_IXGRP) ? "x" : "-");
    printf((mode & S_IROTH) ? "r" : "-");
    printf((mode & S_IWOTH) ? "w" : "-");
    printf((mode & S_IXOTH) ? "x" : "-");
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";

    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    struct dirent *entry;
    char fullpath[4096];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(fullpath, &st) == -1) {
            perror("lstat");
            continue;
        }

        print_permissions(st.st_mode);
        printf(" %2u", (unsigned)st.st_nlink);

        struct passwd* pw = getpwuid(st.st_uid);
        struct group* gr = getgrgid(st.st_gid);
        printf(" %s %s", pw ? pw->pw_name : "?", gr ? gr->gr_name : "?");

        printf(" %8ld", st.st_size);

        char timebuf[80];
        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", localtime(&st.st_mtime));
        printf(" %s", timebuf);

        printf(" %s", entry->d_name);

        if (S_ISLNK(st.st_mode)) {
            char link[1024];
            ssize_t len = readlink(fullpath, link, sizeof(link) - 1);
            if (len != -1) {
                link[len] = '\0';
                printf(" -> %s", link);
            }
        }
        printf("\n");
    }

    closedir(dir);
}
