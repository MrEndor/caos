#define _GNU_SOURCE
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <iostream>

constexpr size_t kStackSize = 8 * 1024 * 1024;

struct ThreadArgs {
  std::function<void()> func;
  std::atomic<bool>* started;
};

int thread_func(void* arg) {
  ThreadArgs* args = static_cast<ThreadArgs*>(arg);

  if (args->started) {
    args->started->store(true);
  }

  try {
    args->func();
  } catch (...) {
  }

  delete args;
  return 0;
}

class Thread {
 public:
  Thread() = default;

  template <typename Func>
  explicit Thread(Func&& f)
      : tid_(-1), stack_(nullptr), joinable_(false), started_(false) {
    start(std::forward<Func>(f));
  }

  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  Thread(Thread&& other) noexcept
      : tid_(other.tid_), stack_(other.stack_), joinable_(other.joinable_) {
    other.tid_ = -1;
    other.stack_ = nullptr;
    other.joinable_ = false;
  }

  ~Thread() {
    if (joinable_) {
      std::terminate();
    }
    if (stack_) {
      free(stack_);
    }
  }

  template <typename Func>
  void start(Func&& f) {
    stack_ = malloc(kStackSize);
    if (!stack_) {
      throw std::runtime_error("Failed to allocate stack");
    }

    ThreadArgs* args =
        new ThreadArgs{std::function<void()>(std::forward<Func>(f)), &started_};

    int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
                CLONE_THREAD | CLONE_SYSVSEM | CLONE_PARENT_SETTID |
                CLONE_CHILD_CLEARTID;

    void* stack_top = static_cast<char*>(stack_) + kStackSize;

    tid_ = clone(thread_func, stack_top, flags, args, &tid_, nullptr, &tid_);

    if (tid_ == -1) {
      delete args;
      free(stack_);
      stack_ = nullptr;
      throw std::runtime_error(std::string("clone failed: ") + strerror(errno));
    }

    joinable_ = true;

    while (!started_.load()) {
      sched_yield();
    }
  }

  void join() {
    if (!joinable_) {
      throw std::runtime_error("Thread is not joinable");
    }

    while (true) {
      int val = tid_;
      if (val == 0) {
        break;
      }

      syscall(SYS_futex, &tid_, FUTEX_WAIT, val, nullptr, nullptr, 0);
    }

    joinable_ = false;
  }

  void detach() {
    if (!joinable_) {
      throw std::runtime_error("Thread is not joinable");
    }
    joinable_ = false;
  }

  bool joinable() const { return joinable_; }

  pid_t get_tid() const { return tid_; }

  static pid_t current_tid() { return syscall(SYS_gettid); }

  static pid_t current_tgid() { return getpid(); }

  static int send_signal_to_thread(pid_t tid, int sig) {
    return syscall(SYS_tgkill, getpid(), tid, sig);
  }

private:
  pid_t tid_ = -1;
  void* stack_ = nullptr;
  bool joinable_ = false;
  std::atomic<bool> started_ = false;
};

void print_thread_info(const char* name) {
  pid_t tid = Thread::current_tid();
  pid_t tgid = Thread::current_tgid();
  printf("[%s] tid=%d, tgid=%d (pid=%d)\n", name, tid, tgid, getpid());
}

void worker_function(int id) {
  print_thread_info(("Worker " + std::to_string(id)).c_str());

  for (int i = 0; i < 3; i++) {
    printf("  Worker %d: iteration %d\n", id, i);
    usleep(100000);
  }

  printf("  Worker %d: done\n", id);
}

void signal_handler(int sig) {
  printf("Thread %d received signal %d\n", Thread::current_tid(), sig);
}

int main() {
  print_thread_info("Main thread");
  printf("\n");

  signal(SIGUSR1, signal_handler);

  printf("Creating threads...\n");

  Thread t1([]() { worker_function(1); });
  Thread t2([]() { worker_function(2); });

  printf("Threads created:\n");
  printf("  Thread 1 tid: %d\n", t1.get_tid());
  printf("  Thread 2 tid: %d\n", t2.get_tid());
  printf("\n");

  printf("Sending SIGUSR1 to thread 1...\n");
  Thread::send_signal_to_thread(t1.get_tid(), SIGUSR1);
  usleep(50000);

  printf("\nWaiting for threads to complete...\n");
  t1.join();
  t2.join();

  printf("\nAll done!\n");
  return 0;
}
