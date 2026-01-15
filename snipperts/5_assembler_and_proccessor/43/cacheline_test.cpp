#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>

constexpr size_t ARRAY_SIZE = 100'000'000;

int main() {
    for (int64_t step = 1; step < 1024; step *= 2) {
        std::vector<int> array(ARRAY_SIZE, 0);

        auto start = std::chrono::steady_clock::now();

        int sum = 0;
        for (size_t i = 0; i < ARRAY_SIZE; i += step) {
            sum += array[i];
        }

        auto end = std::chrono::steady_clock::now();

        auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double per_iter = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed_ms).count() / (ARRAY_SIZE / step);

        std::cout << "STEP=" << step
                  << ", Time=" << elapsed_ms.count() << " ms"
                  << ", per iteration=" << per_iter << " ns\n";
    }
}
