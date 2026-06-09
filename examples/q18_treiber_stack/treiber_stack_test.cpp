// Вопрос 18: lock-free / wait-free структуры данных, стек Трайбера (Treiber stack).
//
// Демонстрирует lock-free стек на одном CAS-цикле:
//   - push: new Node, CAS на head (release);
//   - pop:  CAS на head (acquire), возвращает std::optional<T>.
// Многопоточный тест: N продюсеров пушат, M консьюмеров попают; проверяем, что
// сумма вытащенных значений == сумме запушенных и что вытащено ровно столько же
// элементов (без потерь и дубликатов).
//
// ── lock-free vs wait-free ────────────────────────────────────────────────────
//   lock-free: гарантируется прогресс СИСТЕМЫ — на любом шаге хотя бы один поток
//              продвигается; ни один поток не может заблокировать остальных
//              навсегда (нет мьютексов, нет priority inversion / deadlock).
//   wait-free: СИЛЬНЕЕ — КАЖДЫЙ поток завершает операцию за конечное (ограниченное)
//              число собственных шагов, независимо от поведения других.
//   Стек Трайбера — LOCK-FREE, но НЕ wait-free: под высокой конкуренцией CAS
//   у конкретного потока может фейлиться сколь угодно много раз подряд (его всё
//   время "обгоняют"), хотя система в целом всегда прогрессирует.
//
// ── Проблема освобождения памяти (memory reclamation) — это вопрос 20 ──────────
//   Здесь в pop мы НЕ вызываем delete на снятой ноде, а складываем её в leak-пул
//   и освобождаем всё в конце, когда потоки уже завершены. Причина: если сделать
//   `delete old` сразу после успешного CAS, другой поток может в этот момент
//   держать тот же указатель в своей локальной `old`/`old->next` и обращаться к
//   уже освобождённой памяти (use-after-free). С этим же связана проблема ABA:
//   адрес ноды может быть переиспользован new'ом и CAS ошибочно "успеет".
//   Безопасные решения (hazard pointers, RCU/epoch, atomic_shared_ptr) — в q20.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q18_treiber_stack/treiber_stack_test.cpp -o bin/q18_test -lgtest -lgtest_main -pthread -latomic
// run:     ./bin/q18_test

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

template <class T>
class TreiberStack {
public:
    struct Node {
        T          value;
        Node*      next;
        explicit Node(const T& value) : value(value), next(nullptr) {}
    };

    void push(const T& value) {
        Node* node = new Node(value);
        // Читаем текущую вершину один раз, дальше CAS-цикл сам обновляет expected.
        node->next = head_.load(std::memory_order_relaxed);
        // release: запись value/next ноды должна быть видна потоку, который её
        // снимет с acquire-загрузкой head в pop (синхронизация happens-before).
        while (!head_.compare_exchange_weak(
                   node->next, node,
                   std::memory_order_release,   // успех
                   std::memory_order_relaxed))  // провал: node->next уже перезаписан
        {
            // compare_exchange_weak при провале кладёт актуальный head в node->next,
            // повторяем CAS. weak допускает spurious-провалы — это ок в цикле.
        }
    }

    std::optional<T> pop() {
        // acquire: увидеть всё, что записал push, поднявший эту вершину.
        Node* old = head_.load(std::memory_order_acquire);
        while (old != nullptr &&
               !head_.compare_exchange_weak(
                   old, old->next,
                   std::memory_order_acquire,   // успех
                   std::memory_order_acquire))  // провал: перечитать old
        {
            // old обновлён актуальным head, повторяем.
        }
        if (old == nullptr) {
            return std::nullopt;
        }
        T value = old->value;
        retire(old);   // НЕ delete сразу — см. шапку (memory reclamation, q20).
        return value;
    }

    // Освобождение отложенных нод. Вызывать ТОЛЬКО когда все потоки завершены.
    ~TreiberStack() {
        for (Node* node = head_.load(); node != nullptr;) {
            Node* next_node = node->next;
            delete node;
            node = next_node;
        }
        for (Node* node : retired_) {
            delete node;
        }
    }

private:
    // "Отложенные" ноды копим в защищённый мьютексом пул — это лишь утилита демо
    // для корректного освобождения в конце, к lock-free алгоритму не относится.
    void retire(Node* node) {
        std::lock_guard<std::mutex> lock(retire_mtx_);
        retired_.push_back(node);
    }

    std::atomic<Node*>  head_{nullptr};
    std::mutex          retire_mtx_;
    std::vector<Node*>  retired_;
};

// Разделяемые счётчики прогона: суммы запушенного/вытащенного, число pop'ов и флаг
// завершения продюсеров.
struct DemoStats {
    std::atomic<long long> pushed_sum{0};
    std::atomic<long long> popped_sum{0};
    std::atomic<long long> popped_cnt{0};
    std::atomic<bool>      producers_done{false};
};

// Запускает продюсеров: каждый пушит per_producer значений 1 (для удобной
// проверки суммы) и аккумулирует локальную сумму в общий счётчик.
static void spawn_producers(std::vector<std::thread>& threads,
                            TreiberStack<int>& stack, DemoStats& stats,
                            int producers, int per_producer) {
    for (int producer = 0; producer < producers; ++producer) {
        threads.emplace_back([&stack, &stats, per_producer] {
            long long local_pushed = 0;
            for (int i = 0; i < per_producer; ++i) {
                stack.push(1);
                local_pushed += 1;
            }
            stats.pushed_sum.fetch_add(local_pushed, std::memory_order_relaxed);
        });
    }
}

// Запускает консьюмеров: крутятся, пока продюсеры не закончили ИЛИ стек не пуст,
// аккумулируют сумму и число вытащенных элементов.
static void spawn_consumers(std::vector<std::thread>& threads,
                            TreiberStack<int>& stack, DemoStats& stats,
                            int consumers) {
    for (int consumer = 0; consumer < consumers; ++consumer) {
        threads.emplace_back([&stack, &stats] {
            long long sum = 0;
            long long count = 0;
            while (true) {
                if (auto value = stack.pop()) {
                    sum += *value;
                    ++count;
                } else if (stats.producers_done.load(std::memory_order_acquire)) {
                    // Финальная подчистка: вдруг что-то залетело после флага.
                    if (auto value = stack.pop()) {
                        sum += *value;
                        ++count;
                    } else {
                        break;
                    }
                }
            }
            stats.popped_sum.fetch_add(sum, std::memory_order_relaxed);
            stats.popped_cnt.fetch_add(count, std::memory_order_relaxed);
        });
    }
}

// ───────────────────────── тесты ─────────────────────────

// Однопоточный LIFO: push 1,2,3 -> pop отдаёт 3,2,1.
TEST(TreiberStack, SingleThreadPushPop) {
    TreiberStack<int> stack;
    stack.push(1);
    stack.push(2);
    stack.push(3);

    EXPECT_EQ(stack.pop(), std::optional<int>{3});
    EXPECT_EQ(stack.pop(), std::optional<int>{2});
    EXPECT_EQ(stack.pop(), std::optional<int>{1});
    EXPECT_EQ(stack.pop(), std::nullopt);
}

// N продюсеров / M консьюмеров: ничего не потеряно и не задублировано.
TEST(TreiberStack, ConcurrentIntegrity) {
    constexpr int kProducers   = 6;
    constexpr int kConsumers   = 6;
    constexpr int kPerProducer = 200'000;

    TreiberStack<int> stack;
    DemoStats stats;
    std::vector<std::thread> threads;

    spawn_producers(threads, stack, stats, kProducers, kPerProducer);
    spawn_consumers(threads, stack, stats, kConsumers);

    for (int producer = 0; producer < kProducers; ++producer) {
        threads[static_cast<std::size_t>(producer)].join();
    }
    stats.producers_done.store(true, std::memory_order_release);
    for (int consumer = kProducers; consumer < kProducers + kConsumers; ++consumer) {
        threads[static_cast<std::size_t>(consumer)].join();
    }

    const long long expected = static_cast<long long>(kProducers) * kPerProducer;
    EXPECT_EQ(stats.pushed_sum.load(), expected);   // запушено ровно столько
    EXPECT_EQ(stats.popped_cnt.load(), expected);   // вытащено ровно столько (нет потерь/дублей)
    EXPECT_EQ(stats.popped_sum.load(), expected);   // суммы сходятся
}

// pop пустого стека возвращает std::nullopt.
TEST(TreiberStack, PopEmptyReturnsNullopt) {
    TreiberStack<int> stack;
    EXPECT_EQ(stack.pop(), std::nullopt);
}
