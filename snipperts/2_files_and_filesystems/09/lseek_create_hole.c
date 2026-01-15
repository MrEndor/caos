#include <unistd.h>
#include <fcntl.h>

int main() {
  int fd = open("a.txt", O_WRONLY | O_CREAT, 0644);
  lseek(fd, 1'000'000, SEEK_SET);
  write(fd, "XD", 2);
}
