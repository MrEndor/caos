#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>

void system_v_shm() {
  key_t key = ftok("/tmp", 'S');
  int shmid = shmget(key, 4096, IPC_CREAT | 0666);

  if (fork() == 0) {
    char* shm = (char*)shmat(shmid, NULL, 0);
    sleep(1);
    printf("Child read: %s\n", shm);
    shmdt(shm);
    exit(0);
  }

  char* shm = (char*)shmat(shmid, NULL, 0);
  strcpy(shm, "Hello from parent!");
  printf("Parent wrote: %s\n", shm);
  shmdt(shm);

  wait(NULL);
  shmctl(shmid, IPC_RMID, NULL);
}

void posix_shm() {
  const char* name = "/my_shm";
  int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
  ftruncate(fd, 4096);

  if (fork() == 0) {
    int fd = shm_open(name, O_RDWR, 0666);
    char* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    sleep(1);
    printf("Child read: %s\n", shm);
    munmap(shm, 4096);
    close(fd);
    exit(0);
  }

  char* shm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  strcpy(shm, "Hello via POSIX!");
  printf("Parent wrote: %s\n", shm);
  munmap(shm, 4096);
  close(fd);

  wait(NULL);
  shm_unlink(name);
}

int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "sysv") == 0) {
    system_v_shm();
  } else if (argc > 1 && strcmp(argv[1], "posix") == 0) {
    posix_shm();
  }
  return 0;
}
