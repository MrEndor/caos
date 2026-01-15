#include <thread>
#include <vector>
#include <iostream>
#include <mutex>

std::mutex result_mutex;
long long total = 0;

void sum_range(std::span<int> data, int start, int end) {
    long long partial_sum = 0;
    
    for (int i = start; i < end; ++i) {
        partial_sum += data[i];
    }
    
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        total += partial_sum;
    }
}

int main() {
    std::vector<int> data(10'000'000);
    for (int i = 0; i < 10'000'000; ++i) {
        data[i] = i + 1;
    }
    
    std::thread t1(sum_range, std::span(data), 0, 5'000'000);
    std::thread t2(sum_range, std::span(data), 5'000'000, 10'000'000);
    
    t1.join();
    t2.join();
    
    std::cout << "Total sum: " << total << "\n";  // 500500
}
