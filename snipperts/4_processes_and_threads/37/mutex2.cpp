#define _GNU_SOURCE
#include <linux/futex.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>

class Mutex {
  std::atomic<int> state_{0};

 public:
  void lock() {
    int expected = 0;
    if (state_.compare_exchange_strong(expected, 1)) {
      return;
    }

    while (true) {
      expected = 0;
      if (state_.compare_exchange_strong(expected, 1)) {
        return;
      }
      syscall(SYS_futex, &state_, FUTEX_WAIT, 1, nullptr, nullptr, 0);
    }
  }

  void unlock() {
    state_.store(0);
    syscall(SYS_futex, &state_, FUTEX_WAKE, 1, nullptr, nullptr, 0);
  }
};