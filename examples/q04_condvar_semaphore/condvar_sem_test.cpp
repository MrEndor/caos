// Вопрос 4: condition_variable и counting_semaphore (GoogleTest).
//
// Проверяет инварианты:
//   - producer/consumer на std::condition_variable + std::mutex + std::queue;
//   - std::counting_semaphore<N> как ограничитель параллелизма;
//   - собственную реализацию семафора через condition_variable.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q04_condvar_semaphore/condvar_sem_test.cpp -o bin/q04_test -lgtest -lgtest_main -pthread
// run:     ./bin/q04_test

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <semaphore>
#include <thread>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Собственный семафор поверх condition_variable.
//   Внутри: счётчик разрешений + mutex + cv. acquire ждёт пока count>0,
//   release увеличивает count и будит одного ожидающего.
// ─────────────────────────────────────────────────────────────────────────────
class Semaphore {
public:
    explicit Semaphore(int initial) : count_(initial) {}

    void acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        // Цикл-предикат внутри wait защищает от spurious wakeup (ложного
        // пробуждения): cv может проснуться сам по себе, поэтому условие
        // ПЕРЕпроверяется в цикле, что pred-перегрузка делает автоматически.
        not_zero_.wait(lock, [&] { return count_ > 0; });
        --count_;
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++count_;
        }
        not_zero_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable not_zero_;
    int count_;
};

// Прогоняет threads_count потоков через ограничитель: каждый занимает разрешение,
// удерживает его недолго и фиксирует наблюдаемый максимум одновременной
// активности. Возвращает этот максимум. acquire/release — любой вызываемый
// объект с семантикой семафора.
template <class AcquireFn, class ReleaseFn>
static int measure_max_concurrency(int threads_count, AcquireFn acquire,
                                   ReleaseFn release) {
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};

    std::vector<std::jthread> threads;
    for (int i = 0; i < threads_count; ++i) {
        threads.emplace_back([&] {
            acquire();
            int current = active.fetch_add(1) + 1;
            int previous = max_active.load();
            while (current > previous &&
                   !max_active.compare_exchange_weak(previous, current)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            active.fetch_sub(1);
            release();
        });
    }
    threads.clear();
    return max_active.load();
}

// Producer наполняет очередь, consumer суммирует под предикат-циклом.
// wait(lock, pred) эквивалентен while(!pred()) cv.wait(lock) — защита от
// spurious wakeup. Сумма потреблённого == сумма арифметической прогрессии.
TEST(ConditionVariable, ProducerConsumerSum) {
    constexpr int kItems = 1000;

    std::queue<int> queue;
    std::mutex mutex;
    std::condition_variable not_empty;
    bool done = false;
    long long consumed_sum = 0;

    std::jthread consumer([&] {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            not_empty.wait(lock, [&] { return !queue.empty() || done; });
            if (queue.empty() && done) {
                break;
            }
            int value = queue.front();
            queue.pop();
            lock.unlock(); // обработку делаем вне критической секции
            consumed_sum += value;
        }
    });

    std::jthread producer([&] {
        for (int i = 1; i <= kItems; ++i) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                queue.push(i);
            }
            not_empty.notify_one();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        not_empty.notify_one();
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed_sum, static_cast<long long>(kItems) * (kItems + 1) / 2);
}

// std::counting_semaphore<N> ограничивает число одновременно работающих потоков:
// acquire уменьшает счётчик (блокируясь при нуле), release увеличивает. Под
// лимитом 3 наблюдаемый максимум одновременной активности не превышает 3.
TEST(CountingSemaphore, LimitsConcurrency) {
    constexpr int kPermits = 3;
    constexpr int kThreads = 12;
    std::counting_semaphore<kPermits> sem(kPermits);

    int observed_max = measure_max_concurrency(
        kThreads, [&] { sem.acquire(); }, [&] { sem.release(); });

    EXPECT_LE(observed_max, kPermits);
    EXPECT_GE(observed_max, 1);
}

// Собственный семафор через condition_variable ведёт себя как std: под лимитом 2
// одновременно в секции не более 2 потоков.
TEST(CustomSemaphore, BehavesLikeStd) {
    constexpr int kPermits = 2;
    constexpr int kThreads = 10;
    Semaphore sem(kPermits);

    int observed_max = measure_max_concurrency(
        kThreads, [&] { sem.acquire(); }, [&] { sem.release(); });

    EXPECT_LE(observed_max, kPermits);
    EXPECT_GE(observed_max, 1);
}
