// Вопрос 20: управление памятью в lock-free структурах, std::atomic<shared_ptr>.
//
// Проблема reclamation (когда безопасно delete ноду в lock-free алгоритме):
//   После успешного CAS, снявшего ноду с вершины стека, её НЕЛЬЗЯ сразу удалять —
//   другой поток мог секунду назад прочитать тот же указатель (head) и вот-вот
//   разыменует его (->next, ->value). delete приведёт к use-after-free, а
//   переиспользование адреса аллокатором — к ABA. Нужен механизм, гарантирующий,
//   что ноду удаляют ТОЛЬКО когда на неё точно никто не смотрит.
//
// Подходы (обзор):
//   1) Reference counting у самой ноды — но тогда сам счётчик надо менять
//      атомарно вместе с указателем; наивный refcount страдает тем же ABA.
//   2) Hazard pointers — каждый поток публикует в общий массив указатель, который
//      сейчас читает ("опасный"). Освобождающий поток сканирует массив и
//      откладывает delete, пока его адрес кем-то «защищён». Рабочий упрощённый
//      пример (1 hazard-слот на поток) — ниже, см. HPStack.
//   3) RCU / epoch-based reclamation — читатели заходят в «эпоху», освобождение
//      откладывается до смены эпохи, когда старых читателей гарантированно нет.
//      (Здесь только упоминаем.)
//   4) std::atomic<std::shared_ptr<T>> (C++20) — refcount управляется библиотекой
//      атомарно ВМЕСТЕ с указателем; reclamation автоматическая. Главная часть.
//
// ── Чем atomic<shared_ptr> отличается от обычного shared_ptr ───────────────────
//   Обычный shared_ptr атомарно ведёт ТОЛЬКО control-block refcount, но сам объект
//   shared_ptr (пара указателей ptr+ctrl) НЕ потокобезопасен: одновременные
//   load/store одного и того же shared_ptr — гонка. std::atomic<shared_ptr<T>>
//   делает атомарными И load/store/compare_exchange самого указателя, И корректную
//   работу с refcount при этом. Можно ли lock-free? В принципе да (split reference
//   counting / DWCAS), но libstdc++ реализует atomic<shared_ptr> через внутренние
//   hazard pointers + спинлок-таблицу, поэтому is_lock_free() обычно false.
//   Стоимость: дороже сырого atomic<Node*> (атомарные RMW над refcount, барьеры),
//   зато корректное автоматическое освобождение без ABA и use-after-free.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q20_atomic_shared_ptr/atomic_sp_test.cpp -o bin/q20_test -lgtest -lgtest_main -pthread -latomic
// run:     ./bin/q20_test

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

// ─────────────────────────────────────────────────────────────────────────────
// ГЛАВНАЯ ЧАСТЬ: lock-free* стек через std::atomic<std::shared_ptr<Node>>.
// Освобождение памяти полностью автоматическое: пока кто-то держит shared_ptr на
// ноду (например, прочитал head перед CAS), refcount > 0 и нода жива. Как только
// последний владелец отпускает — control block удаляет ноду. ABA не возникает:
// сравниваются владеющие shared_ptr, а не сырые адреса.
//   * lock-free в смысле алгоритма; физически libstdc++ может использовать
//     внутренний спинлок — см. is_lock_free().
// ─────────────────────────────────────────────────────────────────────────────
template <class T>
class SharedPtrStack {
    struct Node {
        T                     value;
        std::shared_ptr<Node> next;
        explicit Node(const T& value) : value(value) {}
    };

    std::atomic<std::shared_ptr<Node>> head_;

public:
    bool is_lock_free() const { return head_.is_lock_free(); }

    void push(const T& value) {
        auto node = std::make_shared<Node>(value);
        node->next = head_.load(std::memory_order_relaxed);
        // CAS публикует ноду; при провале node->next обновится актуальным head.
        while (!head_.compare_exchange_weak(
                   node->next, node,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    std::optional<T> pop() {
        // Загружаем shared_ptr -> пока он у нас, нода точно не удалится.
        std::shared_ptr<Node> old = head_.load(std::memory_order_acquire);
        while (old &&
               !head_.compare_exchange_weak(
                   old, old->next,
                   std::memory_order_acquire, std::memory_order_acquire)) {
        }
        if (!old) {
            return std::nullopt;
        }
        return old->value;   // old выйдет из области — refcount упадёт сам.
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Упрощённый рабочий пример HAZARD POINTERS поверх стека из сырых указателей.
// 1 hazard-слот на поток. Демонстрирует идею безопасного отложенного delete.
// ─────────────────────────────────────────────────────────────────────────────
template <class T>
class HPStack {
    struct Node {
        T     value;
        Node* next;
        explicit Node(const T& value) : value(value), next(nullptr) {}
    };

    std::atomic<Node*> head_{nullptr};

    static constexpr std::size_t kMaxThreads = 64;
    // Глобальная таблица hazard-указателей: что каждый поток сейчас читает.
    static inline std::atomic<Node*> hazard_[kMaxThreads]{};

    // Per-thread: индекс своего hazard-слота + список нод, ждущих удаления.
    static inline std::atomic<std::size_t> next_slot_{0};
    static std::size_t my_slot() {
        thread_local std::size_t slot = next_slot_.fetch_add(1, std::memory_order_relaxed);
        return slot;
    }
    static thread_local inline std::vector<Node*> retired_;

    // true, если на ноду сейчас указывает чей-нибудь hazard-указатель.
    static bool is_hazardous(Node* node) {
        for (std::size_t i = 0; i < kMaxThreads; ++i) {
            if (hazard_[i].load(std::memory_order_acquire) == node) {
                return true;
            }
        }
        return false;
    }

    void scan_and_free() {
        // Удаляем из retired_ только то, что НЕ защищено ничьим hazard-указателем.
        std::vector<Node*> still_retired;
        for (Node* node : retired_) {
            if (is_hazardous(node)) {
                still_retired.push_back(node);
            } else {
                delete node;
            }
        }
        retired_.swap(still_retired);
    }

public:
    ~HPStack() {
        for (Node* node = head_.load(); node != nullptr;) {
            Node* next_node = node->next;
            delete node;
            node = next_node;
        }
    }

    void push(const T& value) {
        Node* node = new Node(value);
        node->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(node->next, node,
                   std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    std::optional<T> pop() {
        std::size_t slot = my_slot() % kMaxThreads;
        Node* old;
        for (;;) {
            old = head_.load(std::memory_order_acquire);
            if (!old) {
                hazard_[slot].store(nullptr, std::memory_order_release);
                return std::nullopt;
            }
            // Публикуем "я читаю old" и перепроверяем, что head не сменился.
            hazard_[slot].store(old, std::memory_order_seq_cst);
            if (head_.load(std::memory_order_acquire) != old) {
                continue;  // защитили не то
            }
            if (head_.compare_exchange_weak(old, old->next,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                break;
            }
        }
        T value = old->value;
        hazard_[slot].store(nullptr, std::memory_order_release);  // снимаем защиту
        // Безопасно отложить удаление: delete случится в scan, когда никто не читает.
        retired_.push_back(old);
        if (retired_.size() >= 16) {
            scan_and_free();
        }
        return value;
    }
};

// Разделяемые счётчики прогона стека.
struct StackStats {
    std::atomic<long long> pushed{0};
    std::atomic<long long> popped{0};
    std::atomic<long long> popped_count{0};
    std::atomic<bool>      done{false};
};

// Тело продюсера: per_producer раз пушит 1, аккумулирует в общий счётчик.
template <class Stack>
static void producer_body(Stack& stack, StackStats& stats, int per_producer) {
    long long local_pushed = 0;
    for (int i = 0; i < per_producer; ++i) {
        stack.push(1);
        local_pushed += 1;
    }
    stats.pushed.fetch_add(local_pushed);
}

// Тело консьюмера: попает, пока продюсеры не закончили ИЛИ стек не пуст.
template <class Stack>
static void consumer_body(Stack& stack, StackStats& stats) {
    long long sum = 0;
    long long count = 0;
    for (;;) {
        if (auto value = stack.pop()) {
            sum += *value;
            ++count;
        } else if (stats.done.load(std::memory_order_acquire)) {
            if (auto value = stack.pop()) {
                sum += *value;
                ++count;
            } else {
                break;
            }
        }
    }
    stats.popped.fetch_add(sum);
    stats.popped_count.fetch_add(count);
}

// Гоняет N продюсеров / M консьюмеров по стеку, заполняя переданную статистику
// (StackStats содержит atomic-поля и не копируется/не перемещается).
template <class Stack>
static void run_stack(Stack& stack, StackStats& stats) {
    constexpr int kProducers = 6;
    constexpr int kConsumers = 6;
    constexpr int kPerProducer = 100'000;
    std::vector<std::thread> threads;

    for (int producer = 0; producer < kProducers; ++producer) {
        threads.emplace_back([&] { producer_body(stack, stats, kPerProducer); });
    }
    for (int consumer = 0; consumer < kConsumers; ++consumer) {
        threads.emplace_back([&] { consumer_body(stack, stats); });
    }

    for (int producer = 0; producer < kProducers; ++producer) {
        threads[static_cast<std::size_t>(producer)].join();
    }
    stats.done.store(true, std::memory_order_release);
    for (int consumer = kProducers; consumer < kProducers + kConsumers; ++consumer) {
        threads[static_cast<std::size_t>(consumer)].join();
    }
}

// Общая проверка целостности результата прогона.
static void expect_integrity(const StackStats& stats, long long expected) {
    EXPECT_EQ(stats.pushed.load(), expected);
    EXPECT_EQ(stats.popped_count.load(), expected);
    EXPECT_EQ(stats.popped.load(), expected);
}

// ───────────────────────── тесты ─────────────────────────

// atomic<shared_ptr> стек: авто-reclamation, целостность под нагрузкой.
TEST(AtomicSharedPtrStack, ConcurrentIntegrity) {
    SharedPtrStack<int> stack;
    StackStats stats;
    run_stack(stack, stats);
    expect_integrity(stats, 6LL * 100'000);
}

// hazard-pointer стек: отложенный delete, целостность под нагрузкой.
TEST(HazardPointerStack, ConcurrentIntegrity) {
    HPStack<int> stack;
    StackStats stats;
    run_stack(stack, stats);
    expect_integrity(stats, 6LL * 100'000);
}

// Базовая корректность: однопоточный LIFO push/pop на atomic<shared_ptr> стеке.
TEST(AtomicSharedPtr, SingleThreadPushPop) {
    SharedPtrStack<int> stack;
    stack.push(10);
    stack.push(20);
    stack.push(30);

    EXPECT_EQ(stack.pop(), std::optional<int>{30});
    EXPECT_EQ(stack.pop(), std::optional<int>{20});
    EXPECT_EQ(stack.pop(), std::optional<int>{10});
    EXPECT_EQ(stack.pop(), std::nullopt);
}
