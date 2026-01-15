#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main() {
  printf("PID: %d\n", getpid());
  printf("Открытые дескрипторы: 0 (stdin), 1 (stdout), 2 (stderr)\n");

  int fd1 = open("file1.txt", O_RDONLY);
  int fd2 = open("file2.txt", O_WRONLY | O_CREAT, 0644);
  int fd3 = open("file3.txt", O_RDWR | O_CREAT, 0644);

  printf("Открыты новые дескрипторы: %d, %d, %d\n", fd1, fd2, fd3);
  printf("Смотрите /proc/%d/fd\n", getpid());

  sleep(100);

  close(fd1);
  close(fd2);
  close(fd3);
}
