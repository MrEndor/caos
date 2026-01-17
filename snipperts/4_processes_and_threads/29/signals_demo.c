#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

void handler(int sig) {
  printf("Handled signal: %d\n", sig);
}

void send_to_myself() {
  signal(SIGUSR1, handler);
  raise(SIGUSR1);
  kill(getpid(), SIGUSR1);
}

void sleep_until_signal() {
  signal(SIGINT, handler);
  printf("Z-z-z...\n");
  pause();
  printf("wakieee-wakieee\n");
}

int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "wait") == 0) {
    printf("PID=%d ожидает сигналов...\n", getpid());
    signal(SIGTERM, handler);
    signal(SIGUSR1, handler);
    while (1) {
      sleep(1);
    }
  }

  send_to_myself();
  getchar();
  sleep_until_signal();

  return 0;
}
