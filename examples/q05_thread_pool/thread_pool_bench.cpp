// Вопрос 5: GoogleBenchmark для ThreadPool.
//
// Замеряет throughput пула: каждая итерация submit'ит пачку лёгких задач и ждёт
// их результаты через future. Аргумент бенча — число worker-потоков в пуле, так
// видно, как меняется пропускная способность с ростом числа исполнителей.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q05_thread_pool/thread_pool_bench.cpp -o bin/q05_bench -lbenchmark -pthread
// run:     ./bin/q05_bench

#include "thread_pool.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <future>
#include <vector>

namespace {

// Лёгкая, но не сворачиваемая компилятором работа: целочисленная редукция.
std::uint64_t light_work(std::uint64_t seed) {
    std::uint64_t accumulator = seed;
    for (int i = 0; i < 64; ++i) {
        accumulator = accumulator * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    return accumulator;
}

} // namespace

// state.range(0) задаёт число worker-потоков пула. На каждой итерации submit'им
// пачку задач и дожидаемся их через future.get(), затем учитываем обработанные
// элементы для расчёта items/s (throughput).
static void BM_ThreadPoolThroughput(benchmark::State& state) {
    const unsigned worker_count = static_cast<unsigned>(state.range(0));
    constexpr int kBatch = 256;

    ThreadPool pool(worker_count);

    for (auto _ : state) {
        std::vector<std::future<std::uint64_t>> futures;
        futures.reserve(kBatch);
        for (int i = 0; i < kBatch; ++i) {
            futures.push_back(pool.submit(light_work, static_cast<std::uint64_t>(i)));
        }
        std::uint64_t sink = 0;
        for (auto& future : futures) {
            sink += future.get();
        }
        benchmark::DoNotOptimize(sink);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kBatch));
}
BENCHMARK(BM_ThreadPoolThroughput)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

BENCHMARK_MAIN();
