#include <sys/resource.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

void get_limits() {
  struct rlimit limit;

  getrlimit(RLIMIT_STACK, &limit);
  printf("RLIMIT_STACK: soft=%ld, hard=%ld\n",
         (long)limit.rlim_cur, (long)limit.rlim_max);

  getrlimit(RLIMIT_AS, &limit);
  printf("RLIMIT_AS:    soft=%ld, hard=%ld\n",
         (long)limit.rlim_cur, (long)limit.rlim_max);

  getrlimit(RLIMIT_CPU, &limit);
  printf("RLIMIT_CPU:   soft=%ld, hard=%ld\n",
         (long)limit.rlim_cur, (long)limit.rlim_max);
}

void increase_stack() {
  struct rlimit limit;

  getrlimit(RLIMIT_STACK, &limit);
  printf("До:    soft=%ld bytes (%ld MB)\n",
         (long)limit.rlim_cur, (long)limit.rlim_cur / 1024 / 1024);

  limit.rlim_cur = limit.rlim_max;
  setrlimit(RLIMIT_STACK, &limit);

  getrlimit(RLIMIT_STACK, &limit);
  if (limit.rlim_cur == RLIM_INFINITY) {
    printf("После: soft=unlimited\n");
  }
  else {
    printf("После: soft=%ld bytes (%ld MB)\n",
           (long)limit.rlim_cur, (long)limit.rlim_cur / 1024 / 1024);
  }
}

void limit_memory() {
  struct rlimit limit;

  limit.rlim_cur = 50 * 1024 * 1024;
  limit.rlim_max = 50 * 1024 * 1024;
  setrlimit(RLIMIT_AS, &limit);

  printf("Установлен лимит: 50 MB\n");
  printf("Попытка malloc(100 MB)...\n");

  char *ptr = malloc(100 * 1024 * 1024);

  if (ptr == NULL) {
    printf("Результат: malloc вернул NULL (превышен лимит)\n");
  } else {
    printf("Результат: malloc успешен\n");
    free(ptr);
  }
}

void handler(int sig) {
  printf("Получен сигнал SIGXCPU! Превышен лимит CPU времени\n");
  _exit(1);
}

void limit_cpu() {
  struct rlimit limit;

  limit.rlim_cur = 1;
  limit.rlim_max = 5;
  setrlimit(RLIMIT_CPU, &limit);

  signal(SIGXCPU, handler);

  printf("Установлен лимит: 1 секунда CPU времени\n");
  printf("Запускаем бесконечный цикл...\n");

  while (1)
    ;
}

int main() {
  get_limits();
  increase_stack();
  limit_memory();

  printf("\n[Enter]\n");
  getchar();

  limit_cpu();

  return 0;
}
