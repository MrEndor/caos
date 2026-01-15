#include <thread>
#include <mutex>
#include <unistd.h>

std::mutex mtx1, mtx2;

void thread1_func() {
    std::lock_guard<std::mutex> lock1(mtx1);  // захватил mtx1
    sleep(1);  // дать время потоку 2 захватить mtx2
    std::lock_guard<std::mutex> lock2(mtx2);  // ждёт mtx2... DEADLOCK!
}

void thread2_func() {
    std::lock_guard<std::mutex> lock2(mtx2);  // захватил mtx2
    sleep(1);
    std::lock_guard<std::mutex> lock1(mtx1);  // ждёт mtx1... DEADLOCK!
}


int main() {
    std::thread thread1(thread1_func);
    std::thread thread2(thread2_func);

    thread1.join();
    thread2.join();
}
