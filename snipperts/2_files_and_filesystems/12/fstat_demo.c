#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main() {
  int fd = open("testdir/file1.txt", O_RDONLY);

  struct stat st;
  fstat(fd, &st);

  printf("fstat(fd): размер = %ld, inode = %ld\n", st.st_size, st.st_ino);

  close(fd);
  return 0;
}
