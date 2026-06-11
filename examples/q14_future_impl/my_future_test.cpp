// Вопрос 14: реализация std::future и std::promise «с нуля».
//
// Демонстрирует устройство future/promise:
//   - SharedState<T> { mutex, condition_variable, variant<monostate, T, exception_ptr>, ready }
//     — общее heap-состояние, на которое ссылаются и promise, и future.
//   - my::promise<T> { get_future(), set_value(), set_exception() }  — пишущая сторона.
//   - my::future<T>  { get() (блокирует на cv пока !ready), wait() } — читающая сторона.
//   - Поддержка значений И исключений (set_exception -> rethrow в get()).
//
// Неудобства std::future (и этой реализации):
//   * нет .then()        — нельзя навесить продолжение до C++23 (см. q13, самодельный then).
//   * одноразовый        — get() можно вызвать только раз (значение move-аут).
//   * heap allocation    — shared_state всегда выделяется в куче (shared_ptr).
//   * не cancellable      — нет штатного способа отменить ожидающую задачу.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q14_future_impl/my_future_test.cpp -o bin/q14_test -lgtest -lgtest_main -pthread
// run:     ./bin/q14_test

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace my {

// ───────────────────────── общее состояние ─────────────────────────
template <typename T>
struct SharedState {
    std::mutex              mtx;
    std::condition_variable cv;
    // monostate = ещё пусто, T = значение, exception_ptr = ошибка.
    std::variant<std::monostate, T, std::exception_ptr> slot;
    bool                    ready = false;

    void set_value(T value) {
        {
            std::lock_guard lock(mtx);
            if (ready) {
                throw std::logic_error("promise already satisfied");
            }
            slot.template emplace<1>(std::move(value));   // index 1 = T
            ready = true;
        }
        cv.notify_all();   // будим всех, кто ждёт в get()/wait()
    }

    void set_exception(std::exception_ptr eptr) {
        {
            std::lock_guard lock(mtx);
            if (ready) {
                throw std::logic_error("promise already satisfied");
            }
            slot.template emplace<2>(eptr);               // index 2 = exception_ptr
            ready = true;
        }
        cv.notify_all();
    }

    void wait() {
        std::unique_lock lock(mtx);
        // Ждём на cv, пока пишущая сторона не выставит ready (защита от ложных пробуждений).
        cv.wait(lock, [this] { return ready; });
    }

    T get() {
        std::unique_lock lock(mtx);
        cv.wait(lock, [this] { return ready; });
        if (slot.index() == 2) {
            // Внутри была ошибка — пробрасываем её в поток-читатель.
            std::rethrow_exception(std::get<2>(slot));
        }
        return std::move(std::get<1>(slot));   // одноразово отдаём значение (move-out)
    }
};

// предварительное объявление, чтобы promise мог вернуть future
template <typename T> class future;

// ───────────────────────── promise (пишущая сторона) ─────────────────────────
template <typename T>
class promise {
public:
    promise() : state_(std::make_shared<SharedState<T>>()) {}

    future<T> get_future();                          // определено ниже

    void set_value(T value) {
        state_->set_value(std::move(value));
    }
    void set_exception(std::exception_ptr eptr) {
        state_->set_exception(eptr);
    }

private:
    std::shared_ptr<SharedState<T>> state_;
};

// ───────────────────────── future (читающая сторона) ─────────────────────────
template <typename T>
class future {
public:
    future() = default;
    explicit future(std::shared_ptr<SharedState<T>> state) : state_(std::move(state)) {}

    // блокирует, бросает при ошибке
    T get() {
        return state_->get();
    }
    // только дождаться готовности
    void wait() {
        state_->wait();
    }

private:
    std::shared_ptr<SharedState<T>> state_;
};

template <typename T>
future<T> promise<T>::get_future() {
    return future<T>(state_);                         // future делит то же состояние
}

} // namespace my

// ───────────────────────── тесты ─────────────────────────

// producer-поток вызывает set_value, future.get() возвращает то же значение.
TEST(MyFuture, SetValueReturnsValue) {
    my::promise<int> prom;
    my::future<int>  fut = prom.get_future();

    std::thread producer([promise = std::move(prom)]() mutable {
        std::this_thread::sleep_for(20ms);
        promise.set_value(123);                       // отдаём значение
    });

    EXPECT_EQ(fut.get(), 123);
    producer.join();
}

// set_exception упаковывает исключение, get() пробрасывает его в читателя.
TEST(MyFuture, SetExceptionPropagates) {
    my::promise<std::string> prom;
    my::future<std::string>  fut = prom.get_future();

    std::thread producer([promise = std::move(prom)]() mutable {
        std::this_thread::sleep_for(20ms);
        try {
            throw std::runtime_error("что-то сломалось в producer");
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    EXPECT_THROW(fut.get(), std::runtime_error);
    producer.join();
}

// wait() обязан вернуться только после того, как producer выставил значение.
TEST(MyFuture, WaitBlocksUntilReady) {
    my::promise<int>  prom;
    my::future<int>   fut = prom.get_future();
    std::atomic<bool> value_set{false};

    std::thread producer([promise = std::move(prom), &value_set]() mutable {
        std::this_thread::sleep_for(50ms);
        value_set.store(true, std::memory_order_release);
        promise.set_value(7);
    });

    fut.wait();   // должно разблокироваться лишь после set_value
    EXPECT_TRUE(value_set.load(std::memory_order_acquire));
    EXPECT_EQ(fut.get(), 7);
    producer.join();
}
