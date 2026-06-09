// Вопрос 6: GoogleBenchmark — сравнение примитивов синхронизации под contention.
//
// Все потоки инкрементят ОДИН общий счётчик под разными примитивами:
//   - std::atomic<long long>::fetch_add (lock-free);
//   - Spinlock     (TAS на atomic_flag);
//   - TtasSpinlock (TTAS);
//   - std::mutex.
// Бенч многопоточный: ->Threads(n). Чем больше потоков дерётся за одну линию,
// тем сильнее деградация — наглядно видно цену contention на shared-счётчике.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q06_atomic_spinlock/spinlock_bench.cpp -o bin/q06_bench -lbenchmark -pthread -latomic
// run:     ./bin/q06_bench

#include "spinlock.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <mutex>

namespace {

// Общие на весь процесс счётчики и примитивы. В multithreaded-бенче все потоки
// одного запуска делят один и тот же объект — именно это создаёт contention.
std::atomic<long long> g_atomic_counter{0};

long long    g_spin_counter = 0;
Spinlock     g_spinlock;

long long    g_ttas_counter = 0;
TtasSpinlock g_ttas_spinlock;

long long    g_mutex_counter = 0;
std::mutex   g_mutex;

} // namespace

// std::atomic::fetch_add — lock-free инкремент общей кэш-линии.
static void BM_AtomicFetchAdd(benchmark::State& state) {
    if (state.thread_index() == 0) {
        g_atomic_counter.store(0, std::memory_order_relaxed);
    }
    for (auto _ : state) {
        g_atomic_counter.fetch_add(1, std::memory_order_relaxed);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AtomicFetchAdd)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// TAS-спинлок: каждый поток крутит test_and_set, дёргая линию в M-состояние.
static void BM_SpinlockTas(benchmark::State& state) {
    if (state.thread_index() == 0) {
        g_spin_counter = 0;
    }
    for (auto _ : state) {
        SpinGuard<Spinlock> guard(g_spinlock);
        ++g_spin_counter;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpinlockTas)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// TTAS-спинлок: дешёвый load в цикле ожидания снижает трафик когерентности.
static void BM_SpinlockTtas(benchmark::State& state) {
    if (state.thread_index() == 0) {
        g_ttas_counter = 0;
    }
    for (auto _ : state) {
        SpinGuard<TtasSpinlock> guard(g_ttas_spinlock);
        ++g_ttas_counter;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpinlockTtas)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// std::mutex: при contention уходит в сон через futex вместо busy-wait.
static void BM_Mutex(benchmark::State& state) {
    if (state.thread_index() == 0) {
        g_mutex_counter = 0;
    }
    for (auto _ : state) {
        std::lock_guard<std::mutex> guard(g_mutex);
        ++g_mutex_counter;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Mutex)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

// Про порядок памяти и store buffering: на x86 операции store могут быть отложены
// в store buffer, из-за чего поток видит свою запись раньше, чем её увидят другие.
// Именно acquire/release-семантика lock/unlock в spinlock.hpp гарантирует, что
// записи критической секции станут видимы до следующего lock.
// Подробный разбор store buffering — в примере q21.

BENCHMARK_MAIN();
