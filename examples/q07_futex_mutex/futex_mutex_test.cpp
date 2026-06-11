// Вопрос 7: futex и мьютекс на двух состояниях через futex (GoogleTest).
//
// Проверяет инварианты FutexMutex (0=unlocked, 1=locked):
//   lock  = несколько спинов CAS(0->1), затем futex_wait;
//   unlock= store(0) + futex_wake(1).
//
// Идея futex (fast userspace mutex): в неконкурентном случае всё решается
// атомарными операциями в user space БЕЗ системного вызова. Только когда поток
// вынужден ждать, он уходит в ядро. Ядро хранит хэш-таблицу wait-очередей, бакет
// выбирается по физическому адресу futex-слова; FUTEX_WAIT кладёт поток в очередь
// этого бакета, FUTEX_WAKE будит N потоков из неё.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q07_futex_mutex/futex_mutex_test.cpp -o bin/q07_test -lgtest -lgtest_main -pthread
// run:     ./bin/q07_test

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// Состояния двухсостояночного мьютекса (futex word — это u32, поэтому
// underlying type у enum — std::uint32_t).
enum class State : std::uint32_t {
    Unlocked = 0,   // свободен
    Locked   = 1,   // занят
};

// futex_wait: усыпить поток, ПОКА *addr == expected. Если значение уже изменилось,
// возвращается немедленно (это защищает от потери пробуждения). PRIVATE_FLAG —
// futex используется только внутри процесса (быстрее, без учёта shared-памяти).
// Ядро читает 4 байта по адресу atomic'а; expected конвертируем в u32.
static int futex_wait(std::atomic<State>* addr, State expected) {
    return static_cast<int>(syscall(SYS_futex, addr,
                                    FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                                    static_cast<std::uint32_t>(expected),
                                    nullptr, nullptr, 0));
}

// futex_wake: разбудить до wake_count потоков, ждущих на этом адресе.
static int futex_wake(std::atomic<State>* addr, int wake_count) {
    return static_cast<int>(syscall(SYS_futex, addr,
                                    FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                                    wake_count, nullptr, nullptr, 0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Мьютекс на двух состояниях: Unlocked / Locked.
// Минус двухсостояночной схемы: unlock ВСЕГДА делает futex_wake, даже если
// ждущих нет (лишний syscall). Эту проблему решает трёхсостояночный вариант (q08).
// ─────────────────────────────────────────────────────────────────────────────
class FutexMutex {
public:
    void lock() {
        // Fast path + короткий спин: пробуем Unlocked -> Locked без ухода в ядро.
        for (int spin = 0; spin < 40; ++spin) {
            State expected = State::Unlocked;
            if (state_.compare_exchange_strong(expected, State::Locked,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                return;
            }
            std::this_thread::yield();
        }
        // Slow path: спин не помог — засыпаем в ядре, пока state остаётся Locked.
        for (;;) {
            State expected = State::Unlocked;
            if (state_.compare_exchange_strong(expected, State::Locked,
                                               std::memory_order_acquire,
                                               std::memory_order_relaxed)) {
                return;
            }
            futex_wait(&state_, State::Locked);  // спим, пока *state == Locked
        }
    }

    void unlock() {
        state_.store(State::Unlocked, std::memory_order_release);
        // Будим одного ожидающего. В 2-state делаем это всегда (даже без ждущих).
        futex_wake(&state_, 1);
    }

private:
    std::atomic<State> state_{State::Unlocked};
};

// N потоков инкрементят общий счётчик под FutexMutex. Если взаимное исключение
// работает, итог точно равен kThreads * kIters без потерянных инкрементов.
TEST(FutexMutex, CounterIsExact) {
    constexpr int kThreads = 8;
    constexpr int kIters = 100'000;

    FutexMutex mutex;
    long long counter = 0;

    std::vector<std::jthread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int k = 0; k < kIters; ++k) {
                mutex.lock();
                ++counter;
                mutex.unlock();
            }
        });
    }
    threads.clear();

    EXPECT_EQ(counter, static_cast<long long>(kThreads) * kIters);
}

// Прямая проверка взаимного исключения: внутри критической секции флаг in_section
// никогда не должен быть установлен другим потоком. Если бы два потока вошли в
// секцию одновременно, мы зафиксировали бы нарушение.
TEST(FutexMutex, MutualExclusion) {
    constexpr int kThreads = 6;
    constexpr int kIters = 50'000;

    FutexMutex mutex;
    std::atomic<bool> in_section{false};
    std::atomic<long long> violations{0};

    std::vector<std::jthread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int k = 0; k < kIters; ++k) {
                mutex.lock();
                // Внутри секции владелец должен быть единственным.
                bool was_occupied = in_section.exchange(true, std::memory_order_acq_rel);
                if (was_occupied) {
                    violations.fetch_add(1, std::memory_order_relaxed);
                }
                in_section.store(false, std::memory_order_release);
                mutex.unlock();
            }
        });
    }
    threads.clear();

    EXPECT_EQ(violations.load(), 0);
}
