#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void simple_handler(int sig) {
  write(STDOUT_FILENO, "SIGINT caught!\n", 15);
}

void segfault_handler(int sig) {
  write(STDOUT_FILENO, "SIGSEGV caught! Exiting...\n", 28);
  _exit(1);
}

void cause_segfault() {
  signal(SIGSEGV, segfault_handler);

  printf("Вызываем segfault...\n");
  int *ptr = NULL;
  *ptr = 42;

  printf("Эта строка не выполнится\n");
}

void handler(int signum) {
  printf("Signal %d caught\n", signum);
  sleep(5);
}

void demo_signal_mask() {
  struct sigaction sa;

  printf("PID=%d\n", getpid());

  sa.sa_handler = &handler;
  sigfillset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("Failed sigaction");
    exit(1);
  }

  sleep(20);
}

int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "segfault") == 0) {
    cause_segfault();
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "mask") == 0) {
    demo_signal_mask();
    return 0;
  }

  return 0;
}
