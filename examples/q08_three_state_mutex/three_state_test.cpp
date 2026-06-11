// Вопрос 8: мьютекс на трёх состояниях (GoogleTest).
//
// Проверяет инварианты:
//   - ThreeStateMutex: 0=unlocked, 1=locked без ждущих, 2=locked со ждущими;
//   - мотивацию: unlock делает дорогой futex_wake ТОЛЬКО когда есть ждущие
//     (состояние было 2), иначе просто store(0) без системного вызова;
//   - сравнение: число futex_wake у 2-state и 3-state под низкой конкуренцией —
//     3-state делает заметно меньше syscall'ов.
//
// OneShot destroyer race: в наивной 2-state схеме unlock = store(0); wake(),
// но между store и wake мьютекс уже свободен, владелец объекта мог его удалить,
// и wake обратится к освобождённой памяти. 3-state уменьшает окно (wake
// вызывается лишь когда состояние было 2), а в реальных рантаймах сочетается
// с тем, что объект нельзя разрушать, пока на нём кто-то ждёт. Здесь —
// упрощённая иллюстрация.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q08_three_state_mutex/three_state_test.cpp -o bin/q08_test -lgtest -lgtest_main -pthread
// run:     ./bin/q08_test

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// Глобальные счётчики реально выполненных futex_wake-сисколлов — для сравнения.
static std::atomic<long long> g_wake_calls_2state{0};
static std::atomic<long long> g_wake_calls_3state{0};

// Обёртки шаблонны по enum-типу состояния: у обоих мьютексов своё enum class State
// с underlying type std::uint32_t (размер futex-слова). Ядро читает 4 байта по
// адресу atomic'а; expected конвертируем в u32.
template <class State>
static int futex_wait(std::atomic<State>* addr, State expected) {
    return static_cast<int>(syscall(SYS_futex, addr,
                                    FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                                    static_cast<std::uint32_t>(expected),
                                    nullptr, nullptr, 0));
}
template <class State>
static int futex_wake(std::atomic<State>* addr, int wake_count) {
    return static_cast<int>(syscall(SYS_futex, addr,
                                    FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                                    wake_count, nullptr, nullptr, 0));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2-состояночный мьютекс (как в q07): unlock ВСЕГДА делает futex_wake.
// ─────────────────────────────────────────────────────────────────────────────
class TwoStateMutex {
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
        g_wake_calls_2state.fetch_add(1, std::memory_order_relaxed);
        futex_wake(&state_, 1); // безусловный wake — лишний syscall, если ждущих нет
    }
private:
    std::atomic<State> state_{State::Unlocked};
};

// ─────────────────────────────────────────────────────────────────────────────
// 3-состояночный мьютекс.
//   0 = свободен, 1 = занят без ждущих, 2 = занят со ждущими.
// lock:  быстрый путь CAS(0->1); иначе xchg(2) и futex_wait, пока state != 0.
// unlock: если прежнее состояние было 2 (были ждущие) -> store(0)+wake;
//         если 1 -> просто store(0) БЕЗ syscall. В этом вся экономия.
// ─────────────────────────────────────────────────────────────────────────────
class ThreeStateMutex {
public:
    enum class State : std::uint32_t {
        Unlocked      = 0,   // свободен
        Locked        = 1,   // занят, ждущих нет
        LockedWaiters = 2,   // занят, есть ждущие
    };

    void lock() {
        // Fast path: Unlocked -> Locked (заняли, ждущих нет).
        State expected = State::Unlocked;
        if (state_.compare_exchange_strong(expected, State::Locked,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
            return;
        }

        // Contended path: помечаем мьютекс как LockedWaiters (xchg) и спим, пока
        // обмен не вернёт Unlocked (то есть мьютекс стал свободен).
        while (state_.exchange(State::LockedWaiters,
                               std::memory_order_acquire) != State::Unlocked) {
            futex_wait(&state_, State::LockedWaiters); // ждём, пока значение LockedWaiters
        }
    }

    void unlock() {
        // exchange на Unlocked возвращает прежнее состояние. Если оно было
        // LockedWaiters — были ждущие, нужен futex_wake. Если Locked — ждущих не
        // было, обходимся без единого системного вызова. На enum нельзя fetch_sub,
        // поэтому используем exchange (он же убирает второй store из старой версии).
        if (state_.exchange(State::Unlocked,
                            std::memory_order_release) == State::LockedWaiters) {
            g_wake_calls_3state.fetch_add(1, std::memory_order_relaxed);
            futex_wake(&state_, 1);
        }
    }

private:
    std::atomic<State> state_{State::Unlocked};
};

// Прогоняет короткую критическую секцию (низкая конкуренция -> часто срабатывает
// fast path) и возвращает финальный счётчик для проверки корректности.
template <class Mutex>
static long long run_bench(Mutex& mutex) {
    constexpr int kThreads = 4;
    constexpr int kIters = 50'000;
    long long counter = 0;
    std::vector<std::jthread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int k = 0; k < kIters; ++k) {
                mutex.lock();
                ++counter; // короткая критическая секция -> низкая конкуренция
                mutex.unlock();
            }
        });
    }
    threads.clear();
    return counter;
}

// ThreeStateMutex обеспечивает взаимное исключение: счётчик точно равен
// kThreads * kIters (4 * 50000 = 200000).
TEST(ThreeStateMutex, CounterIsExact) {
    ThreeStateMutex mutex;
    long long counter = run_bench(mutex);
    EXPECT_EQ(counter, 4LL * 50'000);
}

// Суть вопроса: под низкой конкуренцией 3-state делает МЕНЬШЕ futex_wake-сисколлов,
// чем 2-state (который будит безусловно). Сбрасываем счётчики, гоняем оба
// мьютекса в одинаковых условиях и сравниваем число wake-вызовов.
TEST(ThreeStateMutex, FewerWakesThanTwoState) {
    g_wake_calls_2state.store(0);
    g_wake_calls_3state.store(0);

    TwoStateMutex two_state_mutex;
    ThreeStateMutex three_state_mutex;

    long long two_state_counter = run_bench(two_state_mutex);
    long long three_state_counter = run_bench(three_state_mutex);

    // Оба корректны.
    EXPECT_EQ(two_state_counter, 4LL * 50'000);
    EXPECT_EQ(three_state_counter, 4LL * 50'000);

    long long two_state_wakes = g_wake_calls_2state.load();
    long long three_state_wakes = g_wake_calls_3state.load();

    // 2-state будит на каждом unlock; 3-state — только при наличии ждущих.
    EXPECT_LT(three_state_wakes, two_state_wakes);
}
