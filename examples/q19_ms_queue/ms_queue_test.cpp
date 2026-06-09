// Вопрос 19: lock-free очередь Майкла-Скотта (Michael-Scott queue), проблема ABA.
//
// Демонстрирует lock-free FIFO-очередь:
//   - dummy-нода (фиктивная голова), atomic<Node*> head/tail;
//   - enqueue: CAS добавляет ноду в хвост + "helping" (двигаем tail, если отстал);
//   - dequeue: CAS на head, значение берём из СЛЕДУЮЩЕЙ ноды.
// Тесты: продюсеры/консьюмеры; проверка целостности (сумма enqueued == dequeued).
//
// ── Зачем dummy-нода ──────────────────────────────────────────────────────────
//   Постоянно присутствующая фиктивная нода развязывает head и tail: очередь
//   никогда не пуста "физически" (head всегда указывает на dummy), поэтому
//   enqueue и dequeue не конфликтуют за один и тот же указатель в пустой очереди.
//   Реальное значение всегда лежит в head->next, а не в самой head-ноде.
//
// ── Helping pattern ───────────────────────────────────────────────────────────
//   enqueue делается в ДВА шага: (1) прицепить ноду к last->next, (2) сдвинуть
//   tail на новую ноду. Между ними поток может «уснуть»/вытесниться. Любой другой
//   поток, увидев tail->next != nullptr (tail отстал), ПОМОГАЕТ: сам двигает tail
//   вперёд. Это и обеспечивает lock-freedom — прогресс не зависит от застрявшего.
//
// ── ABA-проблема ──────────────────────────────────────────────────────────────
//   CAS сравнивает только ЗНАЧЕНИЕ указателя. Сценарий: поток T прочитал head=A,
//   собирается CAS(head, A->next). Тем временем другие потоки сняли A, сняли B,
//   освободили A и аллокатор ВЕРНУЛ тот же адрес для НОВОЙ ноды A'. Теперь head
//   снова == A (но это другая нода!). CAS у T ОШИБОЧНО проходит ("A было, A есть"),
//   хотя структура изменилась -> повреждение очереди / use-after-free.
//   Решение — TAGGED POINTER: к указателю приклеивается счётчик версий (tag),
//   инкрементируемый при каждом изменении. CAS сравнивает пару {ptr,tag}, и
//   "A с тегом 5" != "A с тегом 7" — ABA исключена. Ниже показана рабочая
//   реализация tagged-указателя через 128-битный DWCAS (см. struct TaggedPtr).
//   В основной очереди память не освобождаем в рантайме (leak-пул, чистим в
//   деструкторе) — это убирает и use-after-free, и сам триггер ABA; безопасное
//   освобождение (hazard pointers / atomic_shared_ptr) разбирается в q20.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q19_ms_queue/ms_queue_test.cpp -o bin/q19_test -lgtest -lgtest_main -pthread -latomic
// run:     ./bin/q19_test

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

// ─────────────────────────────────────────────────────────────────────────────
// Демонстрация решения ABA через tagged pointer + DWCAS (double-width CAS).
// {ptr, tag} лежат рядом в 16-байтной структуре; std::atomic над ней на x86-64
// компилируется в инструкцию CMPXCHG16B (через libatomic). Тег += 1 на каждое
// изменение -> переиспользование адреса не обманет CAS.
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(16) TaggedPtr {
    void*         ptr = nullptr;
    std::uint64_t tag = 0;
    bool operator==(const TaggedPtr&) const = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// Michael-Scott lock-free queue.
// ─────────────────────────────────────────────────────────────────────────────
template <class T>
class MSQueue {
public:
    struct Node {
        std::optional<T>   value;   // у dummy пусто
        std::atomic<Node*> next{nullptr};
        Node() = default;
        explicit Node(const T& value) : value(value) {}
    };

    MSQueue() {
        Node* dummy = new Node();          // фиктивная нода
        head_.store(dummy);
        tail_.store(dummy);
        track(dummy);
    }

    void enqueue(const T& value) {
        Node* node = new Node(value);
        track(node);
        for (;;) {
            Node* last = tail_.load(std::memory_order_acquire);
            Node* next = last->next.load(std::memory_order_acquire);
            if (last == tail_.load(std::memory_order_acquire)) {  // tail не сменился
                if (next == nullptr) {
                    // Хвост действительно последний — пытаемся прицепить ноду.
                    // release: значение ноды видно тому, кто её dequeue-нет.
                    if (last->next.compare_exchange_weak(
                            next, node,
                            std::memory_order_release, std::memory_order_relaxed)) {
                        // Шаг 2: двигаем tail (не критично, если не выйдет — помогут).
                        tail_.compare_exchange_strong(
                            last, node, std::memory_order_release, std::memory_order_relaxed);
                        return;
                    }
                } else {
                    // tail отстал: ПОМОГАЕМ продвинуть его на уже прицепленную ноду.
                    tail_.compare_exchange_strong(
                        last, next, std::memory_order_release, std::memory_order_relaxed);
                }
            }
        }
    }

    std::optional<T> dequeue() {
        for (;;) {
            Node* first = head_.load(std::memory_order_acquire);
            Node* last  = tail_.load(std::memory_order_acquire);
            Node* next  = first->next.load(std::memory_order_acquire);
            if (first == head_.load(std::memory_order_acquire)) {  // head не сменился
                if (first == last) {
                    if (next == nullptr) {
                        return std::nullopt;                       // очередь пуста
                    }
                    // tail отстал — помогаем, затем повторяем.
                    tail_.compare_exchange_strong(
                        last, next, std::memory_order_release, std::memory_order_relaxed);
                } else {
                    // Значение лежит в next; читаем ДО CAS на head.
                    T value = *next->value;
                    if (head_.compare_exchange_weak(
                            first, next,
                            std::memory_order_acq_rel, std::memory_order_relaxed)) {
                        // first (старый dummy) больше не нужен — НЕ delete (leak-пул,
                        // см. шапку: иначе ABA/use-after-free).
                        return value;
                    }
                }
            }
        }
    }

    ~MSQueue() {
        for (Node* node : tracked_) {
            delete node;
        }
    }

private:
    void track(Node* node) {
        std::lock_guard<std::mutex> lock(track_mtx_);
        tracked_.push_back(node);
    }

    std::atomic<Node*> head_{nullptr};
    std::atomic<Node*> tail_{nullptr};
    std::mutex         track_mtx_;
    std::vector<Node*> tracked_;
};

// Разделяемые счётчики FIFO-прогона: суммы enqueue/dequeue, число dequeue и флаг
// завершения продюсеров.
struct FifoStats {
    std::atomic<long long> enq_sum{0};
    std::atomic<long long> deq_sum{0};
    std::atomic<long long> deq_cnt{0};
    std::atomic<bool>      producers_done{false};
};

// Запускает продюсеров: каждый делает per_producer enqueue(1).
static void spawn_producers(std::vector<std::thread>& threads, MSQueue<int>& queue,
                            FifoStats& stats, int producers, int per_producer) {
    for (int producer = 0; producer < producers; ++producer) {
        threads.emplace_back([&queue, &stats, per_producer] {
            long long sum = 0;
            for (int i = 0; i < per_producer; ++i) {
                queue.enqueue(1);
                sum += 1;
            }
            stats.enq_sum.fetch_add(sum, std::memory_order_relaxed);
        });
    }
}

// Запускает консьюмеров: dequeue, пока продюсеры не закончили ИЛИ очередь не пуста.
static void spawn_consumers(std::vector<std::thread>& threads, MSQueue<int>& queue,
                            FifoStats& stats, int consumers) {
    for (int consumer = 0; consumer < consumers; ++consumer) {
        threads.emplace_back([&queue, &stats] {
            long long sum = 0;
            long long count = 0;
            while (true) {
                if (auto value = queue.dequeue()) {
                    sum += *value;
                    ++count;
                } else if (stats.producers_done.load(std::memory_order_acquire)) {
                    if (auto value = queue.dequeue()) {
                        sum += *value;
                        ++count;
                    } else {
                        break;
                    }
                }
            }
            stats.deq_sum.fetch_add(sum, std::memory_order_relaxed);
            stats.deq_cnt.fetch_add(count, std::memory_order_relaxed);
        });
    }
}

// ───────────────────────── тесты ─────────────────────────

// FIFO-порядок: enqueue 1,2,3 -> dequeue отдаёт 1,2,3.
TEST(MSQueue, FifoOrder) {
    MSQueue<int> queue;
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);

    EXPECT_EQ(queue.dequeue(), std::optional<int>{1});
    EXPECT_EQ(queue.dequeue(), std::optional<int>{2});
    EXPECT_EQ(queue.dequeue(), std::optional<int>{3});
    EXPECT_EQ(queue.dequeue(), std::nullopt);
}

// Продюсеры/консьюмеры: целостность — ничего не потеряно и не задублировано.
TEST(MSQueue, ConcurrentIntegrity) {
    constexpr int kProducers   = 6;
    constexpr int kConsumers   = 6;
    constexpr int kPerProducer = 200'000;

    MSQueue<int> queue;
    FifoStats stats;
    std::vector<std::thread> threads;

    spawn_producers(threads, queue, stats, kProducers, kPerProducer);
    spawn_consumers(threads, queue, stats, kConsumers);

    for (int producer = 0; producer < kProducers; ++producer) {
        threads[static_cast<std::size_t>(producer)].join();
    }
    stats.producers_done.store(true, std::memory_order_release);
    for (int consumer = kProducers; consumer < kProducers + kConsumers; ++consumer) {
        threads[static_cast<std::size_t>(consumer)].join();
    }

    const long long expected = static_cast<long long>(kProducers) * kPerProducer;
    EXPECT_EQ(stats.enq_sum.load(), expected);
    EXPECT_EQ(stats.deq_cnt.load(), expected);
    EXPECT_EQ(stats.deq_sum.load(), expected);
}

// dequeue пустой очереди возвращает std::nullopt.
TEST(MSQueue, DequeueEmptyReturnsNullopt) {
    MSQueue<int> queue;
    EXPECT_EQ(queue.dequeue(), std::nullopt);
}

// Tagged pointer + DWCAS: CAS со старым тегом отвергается, хотя сырой адрес тот же.
TEST(ABA, TaggedPointerPreventsABA) {
    std::atomic<TaggedPtr> ahead{TaggedPtr{}};
    // На x86-64 DWCAS обычно даёт честный lock-free CMPXCHG16B, но это зависит от
    // сборки libatomic — поэтому только информируем, корректность ABA от этого
    // не зависит (сам инвариант ниже не требует lock-free).
    RecordProperty("tagged_ptr_is_lock_free",
                   ahead.is_lock_free() ? "true" : "false");

    int node_a = 0;
    int node_a_reused = 0;

    // Записываем "указатель на A с тегом 1".
    TaggedPtr expected = ahead.load();
    TaggedPtr a_v1{&node_a, expected.tag + 1};
    ASSERT_TRUE(ahead.compare_exchange_strong(expected, a_v1));

    // Имитируем ABA: адрес тот же (&node_a), но прошли изменения -> тег вырос.
    TaggedPtr stale = a_v1;                        // "старое мнение" потока T: A, tag=1
    TaggedPtr a_v3{&node_a, a_v1.tag + 2};         // A вернулся, но tag=3
    ahead.store(a_v3);

    // CAS со старым {A,tag=1} НЕ должен пройти, хотя сырой указатель совпадает.
    bool cas_ok = ahead.compare_exchange_strong(stale, TaggedPtr{&node_a_reused, 99});
    EXPECT_FALSE(cas_ok);
    // ahead остался a_v3 (CAS не тронул значение при провале).
    EXPECT_EQ(ahead.load(), a_v3);
}
