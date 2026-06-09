// Вопрос 5: пул потоков с блокирующей очередью задач (переиспользуемый header).
//
// Содержит:
//   - BlockingQueue<T>: потокобезопасная очередь (mutex + condition_variable),
//     pop() блокируется до появления элемента или закрытия очереди;
//   - ThreadPool: N worker'ов, submit() возвращает std::future<R> через
//     packaged_task, деструктор делает graceful shutdown (close + join).
//
// На этот header ссылаются И тест (thread_pool_test.cpp), И бенч
// (thread_pool_bench.cpp) — примитив вынесен сюда, чтобы не дублировать код.

#ifndef CAOS_Q05_THREAD_POOL_HPP
#define CAOS_Q05_THREAD_POOL_HPP

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Блокирующая очередь: производители кладут push(), потребители забирают pop().
// pop() возвращает std::nullopt, когда очередь закрыта и опустошена — это сигнал
// worker'у завершиться.
// ─────────────────────────────────────────────────────────────────────────────
template <class T>
class BlockingQueue {
public:
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        not_empty_.notify_one();
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        // Ждём, пока появится задача или очередь закроют.
        not_empty_.wait(lock, [&] { return !queue_.empty() || closed_; });
        if (queue_.empty()) { // дошли сюда только потому, что closed_ == true
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all(); // разбудить всех ожидающих, чтобы они вышли
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::queue<T> queue_;
    bool closed_ = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Пул потоков. Задача хранится как std::function<void()>; результат
// возвращается через future, связанный с packaged_task.
// ─────────────────────────────────────────────────────────────────────────────
class ThreadPool {
public:
    explicit ThreadPool(unsigned worker_count) {
        for (unsigned i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    // Деструктор: закрываем очередь и ждём завершения всех worker'ов.
    ~ThreadPool() {
        queue_.close();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // submit упаковывает вызов f(args...) в packaged_task и кладёт в очередь,
    // возвращая future с будущим результатом.
    template <class Func, class... Args>
    auto submit(Func&& func, Args&&... args)
        -> std::future<std::invoke_result_t<Func, Args...>> {
        using Result = std::invoke_result_t<Func, Args...>;

        // shared_ptr, т.к. packaged_task некопируем, а std::function требует
        // копируемую цель.
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

        std::future<Result> future = task->get_future();
        queue_.push([task] { (*task)(); });
        return future;
    }

private:
    void worker_loop() {
        for (;;) {
            auto job = queue_.pop();
            if (!job) {
                break; // очередь закрыта и пуста — выходим
            }
            (*job)();
        }
    }

    BlockingQueue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
};

#endif // CAOS_Q05_THREAD_POOL_HPP
