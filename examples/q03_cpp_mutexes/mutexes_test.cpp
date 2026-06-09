// Вопрос 3: примитивы синхронизации стандартной библиотеки C++ (GoogleTest).
//
// Проверяет инварианты следующих примитивов:
//   - std::mutex + std::lock_guard         — защита счётчика;
//   - std::shared_mutex                     — readers-writers;
//   - std::recursive_mutex                  — повторный захват из того же потока;
//   - std::unique_lock + std::lock(m1,m2)   — захват двух мьютексов без deadlock;
//   - std::scoped_lock(m1,m2)               — то же самое короче (C++17).
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q03_cpp_mutexes/mutexes_test.cpp -o bin/q03_test -lgtest -lgtest_main -pthread
// run:     ./bin/q03_test

#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// std::shared_mutex: много читателей одновременно, один писатель эксклюзивно.
// unique_lock = эксклюзивный (писательский) доступ, shared_lock = разделяемый
// (читательский), который несколько читателей держат одновременно.
// ─────────────────────────────────────────────────────────────────────────────
class SharedMap {
public:
    void put(const std::string& key, int value) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        data_[key] = value;
    }

    int get(const std::string& key) const {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        auto found = data_.find(key);
        if (found == data_.end()) {
            return -1;
        }
        return found->second;
    }

private:
    mutable std::shared_mutex mtx_;
    std::map<std::string, int> data_;
};

// std::recursive_mutex: метод берёт lock и рекурсивно вызывает себя.
// Обычный std::mutex здесь дал бы deadlock на повторном lock из того же потока.
class RecursiveSum {
public:
    // Рекурсивно суммирует count + (count-1) + ... + 0, держа lock на каждом уровне.
    long long sum(int count) {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        if (count <= 0) {
            return 0;
        }
        return count + sum(count - 1); // повторный захват того же мьютекса — ОК
    }

private:
    std::recursive_mutex mutex_;
};

// std::mutex + std::lock_guard: N потоков инкрементят общий счётчик.
// lock_guard захватывает мьютекс в конструкторе и освобождает в деструкторе (RAII).
TEST(Mutex, CounterIsExact) {
    constexpr int kThreads = 8;
    constexpr int kIters = 100'000;

    long long counter = 0;
    std::mutex mutex;

    std::vector<std::jthread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int k = 0; k < kIters; ++k) {
                std::lock_guard<std::mutex> guard(mutex);
                ++counter;
            }
        });
    }
    threads.clear(); // join всех jthread в деструкторе

    EXPECT_EQ(counter, static_cast<long long>(kThreads) * kIters);
}

// Несколько читателей через shared_lock и писатели через unique_lock не должны
// порождать гонок: финальное чтение возвращает одно из записанных значений.
TEST(SharedMutex, ConcurrentReadersNoDataRace) {
    constexpr int kWriters = 2;
    constexpr int kReaders = 6;
    constexpr int kIters = 1000;

    SharedMap shared_map;
    shared_map.put("k0", 0);

    std::atomic<long long> reads_done{0};
    std::vector<std::jthread> threads;

    for (int writer = 0; writer < kWriters; ++writer) {
        threads.emplace_back([&, writer] {
            for (int k = 0; k < kIters; ++k) {
                shared_map.put("k" + std::to_string(writer), k);
            }
        });
    }
    for (int reader = 0; reader < kReaders; ++reader) {
        threads.emplace_back([&] {
            for (int k = 0; k < kIters; ++k) {
                (void)shared_map.get("k0");
                reads_done.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    threads.clear();

    EXPECT_EQ(reads_done.load(), static_cast<long long>(kReaders) * kIters);
    int last = shared_map.get("k0");
    EXPECT_GE(last, 0);
    EXPECT_LT(last, kIters);
}

// recursive_mutex позволяет повторный захват из того же потока: рекурсивная
// сумма 1..100 == 5050 без deadlock.
TEST(RecursiveMutex, AllowsReentrantLock) {
    RecursiveSum recursive_sum;
    EXPECT_EQ(recursive_sum.sum(100), 5050);
}

// std::lock(m1, m2) применяет алгоритм избегания взаимоблокировки: захватывает
// оба мьютекса атомарно. Два потока берут их в РАЗНОМ порядке и не зависают.
TEST(StdLock, NoDeadlockReverseOrder) {
    constexpr int kIters = 50'000;
    std::mutex first_mutex;
    std::mutex second_mutex;
    long long shared = 0;

    auto worker = [&](bool reverse_order) {
        for (int k = 0; k < kIters; ++k) {
            // defer_lock — конструируем unique_lock БЕЗ захвата.
            std::unique_lock<std::mutex> first_lock(first_mutex, std::defer_lock);
            std::unique_lock<std::mutex> second_lock(second_mutex, std::defer_lock);
            // std::lock захватывает оба сразу, избегая deadlock независимо
            // от порядка аргументов.
            if (reverse_order) {
                std::lock(second_lock, first_lock);
            } else {
                std::lock(first_lock, second_lock);
            }
            ++shared;
        }
    };

    std::jthread forward_thread(worker, false);
    std::jthread reverse_thread(worker, true); // обратный порядок — без std::lock был бы deadlock
    forward_thread.join();
    reverse_thread.join();

    EXPECT_EQ(shared, 2LL * kIters);
}

// std::scoped_lock — то же deadlock-free поведение, но компактнее (C++17).
TEST(ScopedLock, MultiMutex) {
    constexpr int kIters = 50'000;
    std::mutex first_mutex;
    std::mutex second_mutex;
    long long shared = 0;

    auto worker = [&] {
        for (int k = 0; k < kIters; ++k) {
            std::scoped_lock lock(first_mutex, second_mutex); // оба мьютекса, без deadlock
            ++shared;
        }
    };

    std::jthread first_thread(worker);
    std::jthread second_thread(worker);
    first_thread.join();
    second_thread.join();

    EXPECT_EQ(shared, 2LL * kIters);
}
