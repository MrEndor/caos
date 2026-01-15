#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

int main() {
  int fd = open("dup_test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);

  int saved_stdout = dup(1);

  dup2(fd, 1);

  printf("Это идет в файл\n");

  dup2(saved_stdout, 1);
  close(saved_stdout); 
  close(fd);

  printf("Это на экран\n");
}
