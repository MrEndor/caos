#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: lsfd <pid>\n");
    return 1;
  }

  char path[256];
  snprintf(path, sizeof(path), "/proc/%s/fd", argv[1]);

  DIR* dir = opendir(path);
  if (!dir) {
    perror("opendir");
    return 1;
  }

  printf("FD\tTarget\n");

  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;

    char fd_path[512];
    snprintf(fd_path, sizeof(fd_path), "%s/%s", path, entry->d_name);

    char target[512];
    ssize_t len = readlink(fd_path, target, sizeof(target) - 1);
    if (len != -1) {
      target[len] = '\0';
      printf("%s\t%s\n", entry->d_name, target);
    }
  }

  closedir(dir);
}
