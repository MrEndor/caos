#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

void simple_fifo() {
  mkfifo("/tmp/fifo", 0666);

  if (fork() == 0) {
    int fd = open("/tmp/fifo", O_RDONLY);
    char buf[100];
    ssize_t n = read(fd, buf, sizeof(buf));
    buf[n] = '\0';
    printf("Read: %s\n", buf);
    close(fd);
    exit(0);
  }

  sleep(1);
  int fd = open("/tmp/fifo", O_WRONLY);
  write(fd, "Hello FIFO", 10);
  close(fd);
  wait(NULL);
  unlink("/tmp/fifo");
}

void multiple_writers() {
  mkfifo("/tmp/fifo", 0666);

  if (fork() == 0) {
    int fd = open("/tmp/fifo", O_RDONLY);
    char buf[100];
    for (int i = 0; i < 3; i++) {
      ssize_t n = read(fd, buf, sizeof(buf));
      buf[n] = '\0';
      printf("Read: %s\n", buf);
    }
    close(fd);
    exit(0);
  }

  sleep(1);

  for (int i = 0; i < 3; i++) {
    if (fork() == 0) {
      int fd = open("/tmp/fifo", O_WRONLY);
      char msg[50];
      snprintf(msg, sizeof(msg), "Writer %d", i + 1);
      write(fd, msg, strlen(msg));
      close(fd);
      exit(0);
    }
  }

  for (int i = 0; i < 4; i++)
    wait(NULL);
  unlink("/tmp/fifo");
}

void multiple_readers() {
  mkfifo("/tmp/fifo", 0666);

  for (int i = 0; i < 3; i++) {
    if (fork() == 0) {
      int fd = open("/tmp/fifo", O_RDONLY);
      char buf[100];
      ssize_t n = read(fd, buf, sizeof(buf));
      if (n > 0) {
        buf[n] = '\0';
        printf("Reader %d: %s\n", i + 1, buf);
      }
      close(fd);
      exit(0);
    }
  }

  sleep(1);

  if (fork() == 0) {
    int fd = open("/tmp/fifo", O_WRONLY);
    write(fd, "Data", 4);
    close(fd);
    exit(0);
  }

  for (int i = 0; i < 3; i++)
    wait(NULL);
  unlink("/tmp/fifo");
}

int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "simple") == 0) {
    simple_fifo();
  }
  else if (argc > 1 && strcmp(argv[1], "writers") == 0) {
    multiple_writers();
  }
  else if (argc > 1 && strcmp(argv[1], "readers") == 0) {
    multiple_readers();
  }
  return 0;
}
