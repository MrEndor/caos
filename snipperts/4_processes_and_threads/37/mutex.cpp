#include <atomic>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cassert>
#include <cstring>

class FutexMutex {
private:
    // 0 = свободно, 1 = занято (и может быть ожидающие)
    std::atomic<uint32_t> state{0};
    
    static int futex_wait(std::atomic<uint32_t>* addr, uint32_t expected) {
        return syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, expected, 
                       nullptr, nullptr, 0);
    }
    
    static int futex_wake(std::atomic<uint32_t>* addr, int num_threads) {
        return syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, num_threads,
                       nullptr, nullptr, 0);
    }
    
public:
    void lock() {
        while (true) {
            uint32_t expected = 0;
            if (state.compare_exchange_strong(
                    expected, 1,
                    std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                return;
            }
            
            if (state.compare_exchange_strong(
                    expected, 2,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                futex_wait(&state, 2);
            }
        }
    }
    
    bool try_lock() {
        uint32_t expected = 0;
        return state.compare_exchange_strong(
            expected, 1,
            std::memory_order_acquire,
            std::memory_order_relaxed);
    }
    
    void unlock() {
        uint32_t old = state.exchange(0, std::memory_order_release);
        
        if (old == 2) {
            futex_wake(&state, 1);
        }
    }
};

#include <thread>
#include <iostream>
#include <vector>
#include <mutex>

FutexMutex mtx;
int counter = 0;

void increment_counter() {
    for (int i = 0; i < 10000; ++i) {
        {
            std::lock_guard<FutexMutex> lock(mtx);
            counter++;
        }
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment_counter);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Counter: " << counter << " (expected 40000)\n";
}
