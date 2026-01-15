#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

using Clock = std::chrono::steady_clock;

int main() {
    std::vector<std::pair<std::size_t, const char*>> levels = {
        {32 * 1024,        "L1"},   // 32 KB
        {256 * 1024,       "L2"},   // 256 KB
        {12 * 1024 * 1024, "L3"},   // 12 MB
        {64 * 1024 * 1024, "RAM"}   // 64 MB
    };

    constexpr std::size_t TOTAL_ACCESSES = 100'000;
    constexpr int REPEATS = 50;
    constexpr size_t PAGE_SIZE = 4096;
    constexpr size_t STRIDE_BYTES = PAGE_SIZE;

    std::mt19937_64 rng{std::random_device{}()};

    for (const auto& [size_bytes, name] : levels) {
        const std::size_t elem_count = size_bytes / sizeof(int);
        const std::size_t stride = STRIDE_BYTES / sizeof(int);

        if (stride == 0 || elem_count < stride) {
            std::cout << name << " — слишком мал для stride\n";
            continue;
        }

        std::size_t accesses_per_pass = elem_count / stride;

        std::size_t passes = (TOTAL_ACCESSES + accesses_per_pass - 1) / accesses_per_pass;

        std::vector<int> array(elem_count, 1);

        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < elem_count; i += stride) {
            indices.push_back(i);
        }

        volatile int warmup = 0;
        for (int w = 0; w < 10; ++w) {
            std::shuffle(indices.begin(), indices.end(), rng);
            for (std::size_t idx : indices) {
                warmup += array[idx];
            }
        }

        long double total_time_ns = 0;
        volatile int sink = 0;

        for (int rep = 0; rep < REPEATS; ++rep) {
            std::shuffle(indices.begin(), indices.end(), rng);

            auto start = Clock::now();

            for (std::size_t pass = 0; pass < passes; ++pass) {
                int sum = 0;
                for (std::size_t idx : indices) {
                    sum += array[idx];
                }
                sink += sum;
            }

            auto end = Clock::now();
            total_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count();
        }

        auto avg_time_ns = total_time_ns / REPEATS;
        auto per_access_ns = avg_time_ns / TOTAL_ACCESSES;

        std::cout << name << " (" << size_bytes / 1024.0 / 1024.0 << " MB)"
                  << ", stride=" << STRIDE_BYTES << "B"
                  << ", passes=" << passes
                  << ", accesses=" << TOTAL_ACCESSES
                  << ", avg=" << avg_time_ns / 1e6 << " ms"
                  << ", per access=" << per_access_ns << " ns\n";
    }
}
