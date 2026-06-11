// Вопрос 9: condition_variable через futex со счётчиком поколений (GoogleTest).
//
// Проверяет инварианты:
//   - FutexMutex — простой мьютекс на futex (вставлен сюда, чтобы файл был
//     самодостаточным);
//   - FutexCondvar — condition variable поверх futex с generation counter (seq):
//       wait(mutex)  = запомнить seq, отпустить mutex, futex_wait(seq, old),
//                      затем снова взять mutex;
//       notify_one() = ++seq, futex_wake(1);
//       notify_all() = ++seq, futex_wake(INT_MAX);
//   - producer/consumer на своём condvar c предикат-циклом.
//
// Зачем generation counter. Проблема lost wakeup: между тем как поток отпустил
// mutex и вызвал futex_wait, другой поток может успеть сделать notify. Если бы мы
// ждали на самом флаге условия, notify прошёл бы "мимо" (futex_wake не имеет
// эффекта, если никто ещё не спит) и поток уснул бы навсегда. seq решает это:
// notify ИНКРЕМЕНТИРУЕТ seq, а futex_wait(&seq, old) просыпается немедленно, если
// seq уже изменился относительно запомненного old — пробуждение не теряется.
//
// Причины spurious wakeup: futex_wait может вернуться без notify (сигнал/EINTR,
// несовпадение значения, конкурентное изменение seq другим notify). Поэтому
// проверять условие надо ВСЕГДА в цикле while(!predicate).
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q09_condvar_futex/condvar_futex_test.cpp -o bin/q09_test -lgtest -lgtest_main -pthread
// run:     ./bin/q09_test

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <queue>
#include <thread>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// Обёртки шаблонны по типу слова: FutexMutex ждёт на enum class State, а
// FutexCondvar — на счётчике поколений seq (uint32_t). Оба типа 4-байтовые;
// expected конвертируем в u32 (для enum это static_cast underlying type, для
// uint32_t — тождество).
template <class Word>
static int futex_wait(std::atomic<Word>* addr, Word expected) {
    return static_cast<int>(syscall(SYS_futex, addr,
                                    FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                                    static_cast<std::uint32_t>(expected),
                                    nullptr, nullptr, 0));
}
template <class Word>
static int futex_wake(std::atomic<Word>* addr, int wake_count) {
    return static_cast<int>(syscall(SYS_futex, addr,
                                    FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                                    wake_count, nullptr, nullptr, 0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Простой мьютекс на futex (2 состояния), чтобы файл был самодостаточным.
// ─────────────────────────────────────────────────────────────────────────────
class FutexMutex {
public:
    enum class State : std::uint32_t {
        Unlocked = 0,
        Locked   = 1,
    };

    void lock() {
        for (;;) {
            State expected = State::Unlocked;
            if (state_.compare_exchange_strong(expected, State::Locked,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                return;
            }
            futex_wait(&state_, State::Locked);
        }
    }
    void unlock() {
        state_.store(State::Unlocked, std::memory_order_release);
        futex_wake(&state_, 1);
    }
private:
    std::atomic<State> state_{State::Unlocked};
};

// ─────────────────────────────────────────────────────────────────────────────
// Condition variable на futex со счётчиком поколений seq.
// ─────────────────────────────────────────────────────────────────────────────
class FutexCondvar {
public:
    // Контракт как у std::condition_variable: вызывается с захваченным mutex.
    void wait(FutexMutex& mutex) {
        // Снимок поколения ДО освобождения мьютекса. Любой notify после этого
        // момента увеличит seq -> futex_wait не уснёт (или сразу проснётся).
        uint32_t old_generation = seq_.load(std::memory_order_acquire);
        mutex.unlock();
        // Спим, только пока seq всё ещё == old_generation. Если notify уже
        // случился — futex_wait вернётся немедленно (EAGAIN), пробуждение не
        // потеряно.
        futex_wait(&seq_, old_generation);
        mutex.lock();
    }

    void notify_one() {
        seq_.fetch_add(1, std::memory_order_release); // новое поколение
        futex_wake(&seq_, 1);
    }

    void notify_all() {
        seq_.fetch_add(1, std::memory_order_release);
        futex_wake(&seq_, INT_MAX); // разбудить всех ждущих
    }

private:
    std::atomic<uint32_t> seq_{0};
};

// producer/consumer на самодельном condvar: producer наполняет очередь items
// значениями, consumer суммирует их под предикат-циклом. Возвращает сумму
// потреблённого — для проверки целостности.
static long long run_producer_consumer(int items) {
    FutexMutex mutex;
    FutexCondvar not_empty;
    std::queue<int> queue;
    bool done = false;
    long long consumed_sum = 0;

    std::jthread consumer([&] {
        for (;;) {
            mutex.lock();
            // Предикат-цикл против spurious wakeup и lost wakeup.
            while (queue.empty() && !done) {
                not_empty.wait(mutex); // wait атомарно отпускает mutex и засыпает
            }
            if (queue.empty() && done) {
                mutex.unlock();
                break;
            }
            int value = queue.front();
            queue.pop();
            mutex.unlock();
            consumed_sum += value;
        }
    });

    std::jthread producer([&] {
        for (int i = 1; i <= items; ++i) {
            mutex.lock();
            queue.push(i);
            mutex.unlock();
            not_empty.notify_one();
        }
        mutex.lock();
        done = true;
        mutex.unlock();
        not_empty.notify_all();
    });

    producer.join();
    consumer.join();
    return consumed_sum;
}

// Сумма потреблённого == сумма арифметической прогрессии 1..kItems. Если бы
// generation counter не защищал от lost wakeup, consumer бы зависал и тест
// никогда не завершился — сам факт прохождения подтверждает корректность.
TEST(FutexCondvar, ProducerConsumerSum) {
    constexpr int kItems = 2000;
    long long consumed_sum = run_producer_consumer(kItems);
    EXPECT_EQ(consumed_sum, static_cast<long long>(kItems) * (kItems + 1) / 2);
}

// Проверка отсутствия lost wakeup: гоняем producer/consumer в отдельном потоке
// под watchdog'ом. Если предикат-цикл и generation counter работают, обмен
// завершится задолго до таймаута. Зависание означало бы потерянное пробуждение.
TEST(FutexCondvar, NoLostWakeup) {
    constexpr int kItems = 5000;
    std::atomic<bool> finished{false};
    long long consumed_sum = 0;

    std::jthread worker([&] {
        consumed_sum = run_producer_consumer(kItems);
        finished.store(true, std::memory_order_release);
    });

    // Watchdog: ждём завершения с разумным запасом, опрашивая флаг.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!finished.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(finished.load(std::memory_order_acquire))
        << "producer/consumer завис — вероятно lost wakeup";
    worker.join();
    EXPECT_EQ(consumed_sum, static_cast<long long>(kItems) * (kItems + 1) / 2);
}
