#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>

int main() {
  printf("ZOMBIE\n");

  pid_t pid = fork();

  if (pid == 0) {
    printf("Child, PID=%d finishing...\n", getpid());
    return 0;
  }

  printf("Parent, PID=%d. Child PID=%d\n", getpid(), pid);
  printf("Check: ps aux | grep %d\n", pid);
  printf("Child will become a zombie (Z) for 5 seconds...\n");

  sleep(5);
  printf("Calling wait() to clean up zombie...\n");
  int status;
  wait(&status);

  printf("Zombie cleaned up. Exit code: %d\n", WEXITSTATUS(status));

  return 0;
}
