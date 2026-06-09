// Вопрос 6: спинлоки на std::atomic (переиспользуемый header).
//
// Содержит:
//   - Spinlock:     TAS-спинлок на std::atomic_flag (test_and_set);
//   - TtasSpinlock: TTAS-вариант (test-and-test-and-set) — дешёвый load в цикле
//                   ожидания, дорогой exchange только когда флаг похоже свободен.
//
// На этот header ссылается бенч (spinlock_bench.cpp) — примитив вынесен сюда,
// чтобы сравнивать реализации, не дублируя код.

#ifndef CAOS_Q06_SPINLOCK_HPP
#define CAOS_Q06_SPINLOCK_HPP

#include <atomic>

#if defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h> // _mm_pause
inline void cpu_relax() { _mm_pause(); }
#else
inline void cpu_relax() {}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// TAS-спинлок на std::atomic_flag.
//   lock:   test_and_set с acquire — крутимся, пока флаг был занят.
//   unlock: clear с release — публикуем все записи критической секции.
//   acquire/release дают корректный happens-before между unlock и следующим lock.
// ─────────────────────────────────────────────────────────────────────────────
class Spinlock {
public:
    void lock() {
        // TAS: повторяем, пока не увидим, что флаг был свободен (вернулся false).
        while (flag_.test_and_set(std::memory_order_acquire)) {
            cpu_relax();
        }
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

// ─────────────────────────────────────────────────────────────────────────────
// TTAS (test-and-test-and-set): сначала читаем флаг обычным load в цикле и
// делаем дорогой exchange только когда флаг похоже свободен. Меньше трафика
// когерентности кэша при высокой конкуренции.
// ─────────────────────────────────────────────────────────────────────────────
class TtasSpinlock {
public:
    void lock() {
        for (;;) {
            // дешёвый цикл ожидания на load (не дёргает кэш-строку в M-состояние)
            while (locked_.load(std::memory_order_relaxed)) {
                cpu_relax();
            }
            // пробуем захватить
            if (!locked_.exchange(true, std::memory_order_acquire)) {
                return;
            }
        }
    }

    void unlock() {
        locked_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> locked_{false};
};

// RAII-обёртка: "мьютекс через спинлок" — захват в конструкторе, освобождение
// в деструкторе, как std::lock_guard.
template <class Lock>
class SpinGuard {
public:
    explicit SpinGuard(Lock& spinlock) : lock_(spinlock) { lock_.lock(); }
    ~SpinGuard() { lock_.unlock(); }
    SpinGuard(const SpinGuard&)            = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;

private:
    Lock& lock_;
};

#endif // CAOS_Q06_SPINLOCK_HPP
