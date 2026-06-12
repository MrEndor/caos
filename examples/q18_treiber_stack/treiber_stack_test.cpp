// Вопрос 18: lock-free / wait-free структуры данных, стек Трайбера (Treiber stack).
//
// Демонстрирует lock-free стек на одном CAS-цикле + СОБСТВЕННЫЙ lock-free
// аллокатор узлов (node pool), чтобы push не дёргал общий malloc:
//   - push: взять узел из пула (lock-free free-list), CAS на head (release);
//   - pop:  CAS на head (acquire), вернуть std::optional<T>.
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
// ── Lock-free «по модулю аллокатора» — и как мы его чиним ─────────────────────
//   Если push зовёт `new`, общий malloc может взять замок арены (в glibc —
//   per-arena mutex): вытеснили поток с этим замком — остальные push'и встали,
//   и строго говоря push НЕ lock-free. Лечится предвыделением: NodePool ниже
//   держит фиксированный массив узлов и lock-free free-list (стек Трайбера из
//   свободных узлов, ABA-safe через tag в голове). allocate/deallocate — это
//   CAS, без malloc. На горячем пути push'а malloc не вызывается вовсе;
//   `new` остаётся лишь как fallback, если пул исчерпан (в конкурентном тесте
//   пул вмещает весь прогон, и fallback'ов ноль — это проверяется).
//
// ── Концепты ──────────────────────────────────────────────────────────────────
//   Storable<T>      — что можно хранить (movable + default-constructible: пул
//                      держит слоты T заранее; настоящие пулы обходятся без
//                      default-construct через placement-new на raw-storage).
//   NodePoolFor<P,T> — контракт аллокатора: allocate()/deallocate()/owns().
//
// ── Проблема освобождения памяти (memory reclamation) — это вопрос 20 ──────────
//   В pop мы НЕ возвращаем узел в пул сразу, а складываем в retired-список и
//   разбираемся в конце, когда потоки уже завершены. Причина: сразу после CAS
//   другой поток может держать тот же указатель в локальной old/old->next и
//   читать уже «освобождённую» память (use-after-free), плюс ABA: пул отдал бы
//   тот же адрес, и CAS ошибочно "успел". Безопасные решения (hazard pointers,
//   RCU/epoch, atomic_shared_ptr) — в q20.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q18_treiber_stack/treiber_stack_test.cpp -o bin/q18_test -lgtest -lgtest_main -pthread -latomic
// run:     ./bin/q18_test

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// ───────────────────────── концепты ─────────────────────────
// Элемент стека: можно перемещать (move ctor + move assign) и держать слот
// заранее (default-construct). Второе — упрощение пула, см. шапку.
template <typename T>
concept Storable = std::movable<T> && std::default_initializable<T>;

// Контракт аллокатора узлов: тип Node, и три lock-free операции над ним.
template <typename Pool, typename T>
concept NodePoolFor = requires(Pool pool, typename Pool::Node* node) {
    typename Pool::Node;
    { pool.allocate() } -> std::same_as<typename Pool::Node*>;
    { pool.deallocate(node) } -> std::same_as<void>;
    { pool.owns(node) } -> std::convertible_to<bool>;
};

// ───────────────────────── lock-free node pool ─────────────────────────
// Фиксированный массив узлов + lock-free free-list свободных. Голова free-list —
// 64-битное слово: старшие 32 бита — tag (против ABA), младшие 32 — индекс+1
// свободного узла (0 = пусто). 64-битный atomic на x86-64 lock-free.
template <Storable T>
class NodePool {
public:
    struct Node {
        T             value{};
        Node*         next = nullptr;     // ссылка для стека Трайбера
        std::uint32_t free_next = 0;      // ссылка free-list: индекс+1, 0 = конец
    };

    explicit NodePool(std::size_t capacity) : nodes_(capacity) {
        // Связать все узлы в начальный free-list: 0 -> 1 -> ... -> n-1 -> end.
        for (std::size_t i = 0; i + 1 < capacity; ++i) {
            nodes_[i].free_next = static_cast<std::uint32_t>(i + 2);  // (i+1)+1
        }
        if (capacity > 0) {
            nodes_[capacity - 1].free_next = 0;                       // конец списка
            head_.store(pack(1, 0), std::memory_order_relaxed);      // голова = узел 0 (+1)
        }
    }

    // Снять узел с free-list. nullptr, если пул исчерпан. Lock-free, ABA-safe.
    Node* allocate() noexcept {
        std::uint64_t old = head_.load(std::memory_order_acquire);
        for (;;) {
            std::uint32_t slot = index_of(old);          // индекс+1
            if (slot == 0) {
                return nullptr;                          // пул пуст
            }
            Node& node = nodes_[slot - 1];
            std::uint64_t next = pack(node.free_next, tag_of(old) + 1);
            if (head_.compare_exchange_weak(old, next,
                    std::memory_order_acquire, std::memory_order_acquire)) {
                return &node;
            }
        }
    }

    // Вернуть узел в free-list. Lock-free, ABA-safe.
    void deallocate(Node* node) noexcept {
        std::uint32_t slot = static_cast<std::uint32_t>(node - nodes_.data()) + 1;
        std::uint64_t old = head_.load(std::memory_order_relaxed);
        for (;;) {
            node->free_next = index_of(old);
            std::uint64_t next = pack(slot, tag_of(old) + 1);
            if (head_.compare_exchange_weak(old, next,
                    std::memory_order_release, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    // Принадлежит ли узел этому пулу (а не fallback-new).
    bool owns(const Node* node) const noexcept {
        return node >= nodes_.data() && node < nodes_.data() + nodes_.size();
    }

private:
    static std::uint64_t pack(std::uint32_t slot, std::uint32_t tag) noexcept {
        return (static_cast<std::uint64_t>(tag) << 32) | slot;
    }
    static std::uint32_t index_of(std::uint64_t value) noexcept {
        return static_cast<std::uint32_t>(value & 0xFFFFFFFFu);
    }
    static std::uint32_t tag_of(std::uint64_t value) noexcept {
        return static_cast<std::uint32_t>(value >> 32);
    }

    std::vector<Node>          nodes_;
    std::atomic<std::uint64_t> head_{0};
};

// ───────────────────────── Treiber stack ─────────────────────────
template <Storable T, NodePoolFor<T> Pool = NodePool<T>>
class TreiberStack {
public:
    using Node = typename Pool::Node;

    explicit TreiberStack(std::size_t pool_capacity = 1024) : pool_(pool_capacity) {}

    void push(T value) {
        Node* node = pool_.allocate();                 // lock-free, без malloc
        if (node == nullptr) {
            node = new Node{};                          // fallback: пул исчерпан
            heap_fallbacks_.fetch_add(1, std::memory_order_relaxed);
        }
        node->value = std::move(value);
        // Читаем текущую вершину один раз, дальше CAS-цикл сам обновляет expected.
        node->next = head_.load(std::memory_order_relaxed);
        // release: запись value/next ноды должна быть видна потоку, который её
        // снимет с acquire-загрузкой head в pop (синхронизация happens-before).
        while (!head_.compare_exchange_weak(
                   node->next, node,
                   std::memory_order_release,   // успех
                   std::memory_order_relaxed))  // провал: node->next уже перезаписан
        {
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
        }
        if (old == nullptr) {
            return std::nullopt;
        }
        T value = old->value;
        retire(old);   // НЕ возвращаем в пул сразу — см. шапку (reclamation, q20).
        return value;
    }

    // Сколько раз push был вынужден уйти в new (пул исчерпан). 0 == пул покрыл всё.
    long long heap_fallbacks() const { return heap_fallbacks_.load(); }

    // Освобождение отложенных нод. Вызывать ТОЛЬКО когда все потоки завершены.
    ~TreiberStack() {
        // Узлы из пула освободит сам pool_ (его vector); удаляем лишь fallback-new.
        auto reclaim = [this](Node* node) {
            if (!pool_.owns(node)) {
                delete node;
            }
        };
        for (Node* node = head_.load(); node != nullptr;) {
            Node* next_node = node->next;
            reclaim(node);
            node = next_node;
        }
        for (Node* node : retired_) {
            reclaim(node);
        }
    }

private:
    // "Отложенные" ноды копим в защищённый мьютексом список — это лишь утилита
    // демо для корректного освобождения в конце, к lock-free алгоритму не
    // относится (безопасный reclamation — q20).
    void retire(Node* node) {
        std::lock_guard<std::mutex> lock(retire_mtx_);
        retired_.push_back(node);
    }

    Pool                   pool_;
    std::atomic<Node*>     head_{nullptr};
    std::mutex             retire_mtx_;
    std::vector<Node*>     retired_;
    std::atomic<long long> heap_fallbacks_{0};
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

// Пул переиспользует освобождённые узлы и не зовёт malloc.
TEST(NodePool, ReusesFreedNodes) {
    NodePool<int> pool(2);
    auto* a = pool.allocate();
    auto* b = pool.allocate();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(pool.allocate(), nullptr);   // capacity 2 -> пул исчерпан
    EXPECT_TRUE(pool.owns(a));

    pool.deallocate(b);                     // вернули узел
    auto* c = pool.allocate();              // снова доступен — без malloc
    EXPECT_EQ(c, b);                        // тот же слот (LIFO free-list)
}

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

// N продюсеров / M консьюмеров: ничего не потеряно и не задублировано, а пул
// вместил весь прогон — push ни разу не ушёл в malloc.
TEST(TreiberStack, ConcurrentIntegrity) {
    constexpr int kProducers   = 6;
    constexpr int kConsumers   = 6;
    constexpr int kPerProducer = 50'000;
    constexpr std::size_t kPoolCapacity =
        static_cast<std::size_t>(kProducers) * kPerProducer;

    TreiberStack<int> stack(kPoolCapacity);   // пул вмещает все узлы прогона
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
    EXPECT_EQ(stack.heap_fallbacks(), 0);           // пул покрыл все push — malloc не звался
}

// pop пустого стека возвращает std::nullopt.
TEST(TreiberStack, PopEmptyReturnsNullopt) {
    TreiberStack<int> stack;
    EXPECT_EQ(stack.pop(), std::nullopt);
}
