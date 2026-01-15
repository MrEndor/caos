#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

int main() {
    constexpr int SIZE = 50'000'000;
    std::vector<int> data(SIZE);

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int& val : data) {
        val = dist(rng);
    }

    using Clock = std::chrono::steady_clock;

    auto start = Clock::now();

    long sum = 0;
    for (int i = 0; i < SIZE; ++i) {
        if (data[i] >= 128) {
            sum += data[i];
        }
    }

    auto end = Clock::now();
    auto time_unsorted = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Unsorted:  " << time_unsorted << " ms  (sum=" << sum << ")\n";

    std::sort(data.begin(), data.end());

    start = Clock::now();

    sum = 0;
    for (int i = 0; i < SIZE; ++i) {
        if (data[i] >= 128) {
            sum += data[i];
        }
    }

    end = Clock::now();
    auto time_sorted = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Sorted:    " << time_sorted << " ms  (sum=" << sum << ")\n";
    std::cout << "Speedup:   " << static_cast<double>(time_unsorted) / time_sorted << "x\n";
}