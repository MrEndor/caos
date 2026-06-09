// Вопрос 5: GoogleTest для ThreadPool и BlockingQueue.
//
// Проверяет корректность примитива из thread_pool.hpp:
//   - future, возвращаемые submit(), отдают правильные результаты;
//   - все поставленные задачи действительно выполняются;
//   - close() будит ожидающих потребителей, pop() отдаёт nullopt.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q05_thread_pool/thread_pool_test.cpp -o bin/q05_test -lgtest -lgtest_main -pthread
// run:     ./bin/q05_test

#include "thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <optional>
#include <thread>
#include <vector>

// future, полученные через submit(), должны отдавать корректные значения i*i.
TEST(ThreadPool, FuturesReturnCorrectResults) {
    constexpr int kTasks = 100;
    long long expected = 0;
    std::vector<std::future<int>> futures;
    futures.reserve(kTasks);

    ThreadPool pool(4);
    for (int i = 1; i <= kTasks; ++i) {
        expected += static_cast<long long>(i) * i;
        futures.push_back(pool.submit([](int value) { return value * value; }, i));
    }

    long long sum = 0;
    for (auto& future : futures) {
        sum += future.get();
    }
    EXPECT_EQ(sum, expected);
}

// Счётчик выполненных задач должен совпасть с числом поставленных.
TEST(ThreadPool, AllTasksExecuted) {
    constexpr int kTasks = 500;
    std::atomic<int> executed{0};

    {
        ThreadPool pool(4);
        for (int i = 0; i < kTasks; ++i) {
            pool.submit([&executed] {
                executed.fetch_add(1, std::memory_order_relaxed);
            });
        }
    } // деструктор пула дожидается выполнения всех задач (graceful shutdown)

    EXPECT_EQ(executed.load(), kTasks);
}

// close() должен разбудить заблокированного на pop() потребителя, и тот должен
// получить std::nullopt (пустая закрытая очередь — сигнал к завершению).
TEST(BlockingQueue, CloseUnblocksConsumers) {
    BlockingQueue<int> queue;
    std::atomic<bool> got_nullopt{false};

    std::thread consumer([&] {
        std::optional<int> value = queue.pop(); // блокируется: очередь пуста
        if (!value.has_value()) {
            got_nullopt.store(true, std::memory_order_relaxed);
        }
    });

    queue.close();  // должен разбудить потребителя
    consumer.join();
    EXPECT_TRUE(got_nullopt.load());
}
