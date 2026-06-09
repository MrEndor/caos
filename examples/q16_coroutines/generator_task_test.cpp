// Вопрос 16: stackful vs stackless корутины, C++20 coroutines, co_await, co_return, co_yield.
//
// Демонстрирует:
//   - Generator<T> (stackless, C++20): promise_type с yield_value + co_yield.
//     Пример — бесконечный fibonacci-генератор, берём первые 10.
//   - Task<T> (stackless): promise_type с return_value/co_return + awaitable.
//     Пример — корутина, которая co_await другую корутину и co_return результат;
//     синхронный драйвер resume()-ит до готовности.
//
// stackful (фибры, q15)         vs   stackless (этот файл, C++20 coroutines)
// ─────────────────────────────────────────────────────────────────────────
//  свой стек на каждую корутину       один кадр (frame) на корутину в куче
//  yield из любой вложенности         co_yield/co_await только в самой корутине
//  переключение = смена rsp           переключение = возврат + resume()
//  не «вирально»                      «вирально»: вызвавшая корутину функция тоже корутина
//  дороже по памяти (целый стек)      дешевле: ровно столько, сколько нужно кадру
//  нельзя без рантайма переключения   компилятор сам режет функцию на состояния
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q16_coroutines/generator_task_test.cpp -o bin/q16_test -lgtest -lgtest_main -pthread
// run:     ./bin/q16_test

#include <coroutine>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// ───────────────────────── Generator<T> (co_yield) ─────────────────────────
template <typename T>
class Generator {
public:
    struct promise_type {
        T current_value{};

        Generator get_return_object() {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // Стартуем «лениво»: не выполняем тело, пока не попросят первое значение.
        std::suspend_always initial_suspend() noexcept { return {}; }
        // final_suspend ОБЯЗАН быть noexcept; suspend_always — чтобы handle не разрушился
        // сам и мы могли корректно его уничтожить из деструктора Generator.
        std::suspend_always final_suspend() noexcept { return {}; }

        // co_yield value -> кладём значение и приостанавливаемся.
        std::suspend_always yield_value(T value) noexcept {
            current_value = std::move(value);
            return {};
        }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };

    using handle_t = std::coroutine_handle<promise_type>;

    explicit Generator(handle_t handle) : handle_(handle) {}
    Generator(const Generator&)            = delete;
    Generator& operator=(const Generator&) = delete;
    Generator(Generator&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    ~Generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // Продвинуть генератор и вернуть очередное значение.
    T next() {
        handle_.resume();                       // выполняем до следующего co_yield
        return handle_.promise().current_value;
    }

private:
    handle_t handle_;
};

// Бесконечный генератор Фибоначчи.
static Generator<std::uint64_t> fibonacci() {
    std::uint64_t prev = 0;
    std::uint64_t curr = 1;
    while (true) {
        co_yield prev;                          // отдаём текущее, приостанавливаемся
        std::uint64_t next = prev + curr;
        prev = curr;
        curr = next;
    }
}

// ───────────────────────── Task<T> (co_await / co_return) ─────────────────────────
template <typename T>
class Task {
public:
    struct promise_type {
        T                    result{};
        std::exception_ptr   error{};
        // Кого возобновить, когда эта задача завершится (заполняется при co_await).
        std::coroutine_handle<> continuation{};

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        // Awaiter для final_suspend: при завершении задачи возобновляем того, кто нас ждал.
        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<>
            await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                auto cont = handle.promise().continuation;
                // symmetric transfer: сразу прыгаем в ожидающую корутину (без рекурсии стека).
                return cont ? cont : std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T value) noexcept { result = std::move(value); }
        void unhandled_exception()           { error = std::current_exception(); }
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

    // Делает Task awaitable: `co_await some_task`.
    struct Awaiter {
        handle_t task;
        bool await_ready() noexcept { return false; }
        // Запоминаем, кого возобновить, и стартуем вложенную задачу.
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
            task.promise().continuation = awaiting;
            return task;                        // symmetric transfer в задачу
        }
        T await_resume() {
            if (task.promise().error) {
                std::rethrow_exception(task.promise().error);
            }
            return std::move(task.promise().result);
        }
    };
    Awaiter operator co_await() noexcept { return Awaiter{handle_}; }

    // Синхронный драйвер: гоняем корутину, пока она не дойдёт до done().
    T run_sync() {
        handle_.resume();
        while (!handle_.done()) {
            handle_.resume();
        }
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
        return std::move(handle_.promise().result);
    }

    handle_t handle_;
};

// Вложенная корутина: возвращает квадрат.
static Task<int> square(int value) {
    co_return value * value;
}

// Корутина, которая co_await другую корутину и co_return результат.
static Task<int> compute() {
    int first  = co_await square(3);            // 9
    int second = co_await square(4);            // 16
    co_return first + second;                   // 25
}

// ───────────────────────── тесты ─────────────────────────

// Генератор отдаёт ровно последовательность Фибоначчи.
TEST(Generator, YieldsFibonacci) {
    auto gen = fibonacci();
    std::vector<std::uint64_t> first_ten;
    for (int i = 0; i < 10; ++i) {
        first_ten.push_back(gen.next());
    }
    std::vector<std::uint64_t> expected{0, 1, 1, 2, 3, 5, 8, 13, 21, 34};
    EXPECT_EQ(first_ten, expected);
}

// Счётчик побочного эффекта: растёт при каждом исполнении тела генератора.
static int g_side_effect_counter = 0;

// Генератор с побочным эффектом перед первым co_yield.
static Generator<int> counting_generator() {
    ++g_side_effect_counter;                    // сработает только после первого resume
    co_yield 1;
    ++g_side_effect_counter;
    co_yield 2;
}

// initial_suspend = suspend_always: тело не исполняется до первого next().
TEST(Generator, LazyEvaluation) {
    g_side_effect_counter = 0;
    auto gen = counting_generator();
    // Генератор создан, но тело ещё не запускалось — счётчик нетронут.
    EXPECT_EQ(g_side_effect_counter, 0);
    EXPECT_EQ(gen.next(), 1);                    // первый resume исполняет тело до co_yield
    EXPECT_EQ(g_side_effect_counter, 1);
}

// Task, которая co_await две вложенные Task, возвращает их сумму квадратов.
TEST(Task, ReturnsAwaitedResult) {
    auto task = compute();                       // square(3)+square(4) = 9+16
    EXPECT_EQ(task.run_sync(), 25);
}
