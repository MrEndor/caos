#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
  int null_fd = open("/dev/null", O_WRONLY);
  write(null_fd, "Эти данные исчезнут\n", 38);
  close(null_fd);

  int zero_fd = open("/dev/zero", O_RDONLY);
  char buf[20];
  read(zero_fd, buf, sizeof(buf));
  for (int i = 0; i < sizeof(buf); i++) {
    printf("%d ", buf[i]);
  }
  printf("\n\n");
  close(zero_fd);

  int rand_fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
  unsigned char random_buf[10];
  ssize_t n = read(rand_fd, random_buf, sizeof(random_buf));
  for (int i = 0; i < n; i++) {
    printf("%02x ", random_buf[i]);
  }
  printf("\n");
  close(rand_fd);

  return 0;
}
