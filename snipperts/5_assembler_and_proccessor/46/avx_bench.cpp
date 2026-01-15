#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <immintrin.h>
#include <cstring>

class AVXBenchmark {
private:
    std::vector<float> a, b, c;
    const std::size_t N;
    const int REPEATS = 100;

public:
    AVXBenchmark(std::size_t size) : N(size) {
        a.resize(N);
        b.resize(N);
        c.resize(N);

        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (std::size_t i = 0; i < N; ++i) {
            a[i] = dist(rng);
            b[i] = dist(rng);
        }
    }

    double scalar_dot_product() {
        double sum = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            sum += static_cast<double>(a[i] * b[i]);
        }
        return sum;
    }

    double avx_dot_product() {
        __m256 sum_vec = _mm256_setzero_ps();

        for (std::size_t i = 0; i + 7 < N; i += 8) {
            __m256 va = _mm256_loadu_ps(&a[i]);
            __m256 vb = _mm256_loadu_ps(&b[i]);
            __m256 prod = _mm256_mul_ps(va, vb);
            sum_vec = _mm256_add_ps(sum_vec, prod);
        }

        float tail_sum = 0.0f;
        for (std::size_t i = (N / 8) * 8; i < N; ++i) {
            tail_sum += a[i] * b[i];
        }

        float sums[8];
        _mm256_storeu_ps(sums, sum_vec);
        double result = tail_sum;
        for (int j = 0; j < 8; ++j) {
            result += sums[j];
        }
        return result;
    }

    template<typename Func>
    std::pair<double, double> benchmark(Func&& func, const std::string& name) {
        func();
        
        long double total_time_ns = 0;
        double result = 0;
        
        for (int rep = 0; rep < REPEATS; ++rep) {
            auto start = std::chrono::steady_clock::now();
            result = func();
            auto end = std::chrono::steady_clock::now();
            
            total_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count();
        }
        
        double avg_time_ns = total_time_ns / REPEATS;
        return {avg_time_ns, result};
    }

    void run() {
        std::cout << "Array size: " << N << " floats (" << N * sizeof(float) / 1024.0 / 1024.0 << " MB)\n\n";

        auto [scalar_time, scalar_res] = benchmark([this]() { return this->scalar_dot_product(); }, "scalar");

        auto [avx_time, avx_res] = benchmark([this]() { return this->avx_dot_product(); }, "AVX");

        std::cout << "=== RESULTS ===\n";
        std::cout << "Scalar:     " << scalar_time << " ns (" << scalar_res << ")\n";
        std::cout << "AVX2:       " << avx_time << " ns (" << avx_res << ")\n";
        std::cout << "Speedup: " << static_cast<double>(scalar_time) / avx_time << "x\n";
    }
};

void matrix_multiply_demo() {
    constexpr int N = 10'000;
    std::vector<float> A(N * N), B(N * N), C(N * N);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& val : A) val = dist(rng);
    for (auto& val : B) val = dist(rng);
    std::fill(C.begin(), C.end(), 0.0f);

    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j < 64; ++j) {
            for (int k = 0; k < 64; ++k) {
                C[i * N + j] += A[i * N + k] * B[k * N + j];
            }
        }
    }

    auto end = Clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

int main() {
    AVXBenchmark bench(10'000'000);
    bench.run();

    matrix_multiply_demo();
}
