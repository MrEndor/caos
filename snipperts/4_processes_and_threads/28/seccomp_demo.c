#include <seccomp.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

void setup_seccomp() {
  scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
  if (ctx == NULL) {
    fprintf(stderr, "Failed to init seccomp\n");
    exit(1);
  }

  seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(clone), 0);
  seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(execve), 0);

  if (seccomp_load(ctx) < 0) {
    fprintf(stderr, "Failed to load seccomp\n");
    exit(1);
  }

  seccomp_release(ctx);
}

int main() {
  setup_seccomp();

  int res = fork();
  if (res == -1) {
    printf("fork() failed: errno=%d (EPERM)\n", errno);
  } else {
    printf("fork() succeeded (UNBELIEVABLE!)\n");
  }

  printf("\n execve() (will cause Bad system call)...\n");
  sleep(1);

  char *argv[] = {"/bin/ls", NULL};
  char *env[] = {NULL};
  execve("/bin/ls", argv, env);
  return 0;
}
