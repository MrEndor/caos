#include <thread>
#include <iostream>

void worker(int id) {
    for (int i = 0; i < 5; ++i) {
        std::cout << "Thread " << id << ": iteration " << i << "\n";
    }
}

int main() {
    std::thread t1(worker, 1);
    
    std::thread t2(worker, 2);
    
    std::cout << "Main thread continues...\n";
    
    t1.join();
    t2.join();
    
    std::cout << "All threads finished\n";
}
