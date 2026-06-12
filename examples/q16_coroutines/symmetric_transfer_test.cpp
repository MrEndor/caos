// Вопрос 16 (symmetric transfer, P0913): почему длинная цепочка co_await не
// переполняет стек.
//
// Демонстрация раздела «Symmetric transfer» из вики
// (sections/concurrency/coroutines_cpp20.md). Ключ — await_suspend возвращает
// std::coroutine_handle<>, и компилятор обязан перейти в handle.resume() через
// ХВОСТОВОЙ вызов (jmp, не call). Поэтому фрейм текущей resume-функции
// переиспользуется, и стек НЕ растёт на каждом звене цепочки.
//
//   - Task<T> с symmetric transfer:
//       * co_await task        -> await_suspend возвращает handle callee  (jump вниз)
//       * final_suspend        -> возвращает continuation либо noop_coroutine (jump вверх)
//   - sum_to(n) рекурсивно co_await'ит sum_to(n-1): цепочка глубиной n.
//   - Тест прогоняет n = 100000. Наивная реализация (cont.resume() обычным
//     вызовом) на такой глубине упала бы со stack overflow; symmetric transfer
//     держит стек постоянным -> тест проходит. Прохождение и есть доказательство.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q16_coroutines/symmetric_transfer_test.cpp -o bin/q16_symmetric_test -lgtest -lgtest_main -pthread
// run:     ./bin/q16_symmetric_test

#include <coroutine>
#include <exception>
#include <utility>

#include <gtest/gtest.h>

// ───────────────────────── Task<T> с symmetric transfer ─────────────────────────
template <typename T>
class Task {
public:
    struct promise_type {
        T result{};
        // Кого возобновить, когда эта задача завершится. По умолчанию — noop:
        // легальная цель для хвостового jmp на верхушке цепочки (см. sync_wait).
        std::coroutine_handle<> continuation = std::noop_coroutine();

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // Ленивый старт: тело не исполняется, пока нас не возобновят.
        std::suspend_always initial_suspend() noexcept { return {}; }

        // final_suspend: задача завершилась — ХВОСТОВЫМ переходом отдаём управление
        // тому, кто нас ждал (continuation). На верхушке цепочки continuation —
        // noop_coroutine, его resume() = ret, управление уходит в caller'а resume().
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<promise_type> self) noexcept {
                return self.promise().continuation;   // tail jump вверх по цепочке
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T value) noexcept { result = std::move(value); }
        void unhandled_exception()           { std::terminate(); }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    explicit Task(handle_t handle) : handle_(handle) {}
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // co_await task: запомнить, кого возобновить по завершении callee, и ХВОСТОВЫМ
    // переходом прыгнуть в callee. Никакого callee.resume() обычным вызовом —
    // именно поэтому стек не растёт.
    struct Awaiter {
        handle_t callee;
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
            callee.promise().continuation = caller;   // куда вернуться
            return callee;                            // tail jump вниз, в callee
        }
        T await_resume() { return std::move(callee.promise().result); }
    };
    Awaiter operator co_await() noexcept { return Awaiter{handle_}; }

    handle_t handle() const noexcept { return handle_; }

private:
    handle_t handle_;
};

// Синхронно дождаться результата корневой задачи. Один resume() запускает всю
// цепочку: вниз — через Awaiter, вверх — через FinalAwaiter, всё хвостовыми
// переходами в пределах одного кадра стека.
template <typename T>
static T sync_wait(Task<T> task) {
    task.handle().resume();
    return std::move(task.handle().promise().result);
}

// Рекурсивная цепочка глубины n: каждое звено co_await'ит следующее.
static Task<long long> sum_to(int n) {
    if (n == 0) {
        co_return 0;
    }
    long long rest = co_await sum_to(n - 1);   // точка приостановки -> symmetric transfer
    co_return n + rest;
}

// ───────────────────────── тесты ─────────────────────────

// Короткая цепочка — проверка корректности результата.
TEST(SymmetricTransfer, ShallowChainIsCorrect) {
    EXPECT_EQ(sync_wait(sum_to(5)), 15);     // 5+4+3+2+1
}

// Очень глубокая цепочка co_await. Без symmetric transfer (наивный
// cont.resume()) стек рос бы на каждое звено и переполнился бы за тысячи шагов.
// С symmetric transfer стек постоянен, ограничены только heap'ом фреймы.
TEST(SymmetricTransfer, DeepChainDoesNotOverflowStack) {
    constexpr int kDepth = 100000;
    long long result = sync_wait(sum_to(kDepth));
    long long expected = 1LL * kDepth * (kDepth + 1) / 2;   // 5000050000
    EXPECT_EQ(result, expected);
}
