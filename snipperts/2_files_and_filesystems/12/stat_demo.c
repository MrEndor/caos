#include <sys/stat.h>
#include <stdio.h>

int main() {
  struct stat st_stat, st_lstat;

  stat("testdir/symlink.txt", &st_stat);
  printf("stat(): размер = %ld (следует по ссылке)\n", st_stat.st_size);

  lstat("testdir/symlink.txt", &st_lstat);
  printf("lstat(): размер = %ld (информация о самой ссылке)\n", st_lstat.st_size);
}
