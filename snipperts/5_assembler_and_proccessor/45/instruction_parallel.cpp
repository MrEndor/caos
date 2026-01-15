#include <iostream>
#include <chrono>

int main() {
    constexpr long iterations = 1'000'000'000L;

    using Clock = std::chrono::steady_clock;

    auto start = Clock::now();

    long x = 0;
    for (long i = 0; i < iterations; ++i) {
        x = x + 1;
        x = x + 1;
        x = x + 1;
    }

    auto end = Clock::now();
    auto time_dependent = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Dependent:    " << time_dependent << " ns, x=" << x << "\n";

    start = Clock::now();

    long a = 0, b = 0, c = 0;
    for (long i = 0; i < iterations; ++i) {
        a = a + 1;
        b = b + 1;
        c = c + 1;
    }

    end = Clock::now();
    auto time_independent = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Independent:  " << time_independent << " ns, a=" << a << ",b=" << b << ",c=" << c << "\n";
    std::cout << "Speedup: " << static_cast<double>(time_dependent) / time_independent << "x\n";
}