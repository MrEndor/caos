// Вопрос 17: GoogleBenchmark — эмпирический эффект false sharing.
//
// Несколько потоков инкрементят atomic-счётчики:
//   - BM_FalseSharing: счётчики лежат на ОДНОЙ кэш-линии -> cache ping-pong;
//   - BM_Padded:       те же счётчики через alignas(64) на РАЗНЫХ линиях -> быстро.
// Бенч многопоточный (->Threads(2)/->Threads(4)); каждый поток бьёт по своему
// счётчику. Padded в разы быстрее (выше items/s, ниже время) — это и есть цена
// ложного разделения.
//
// ── Почему так (MESI) ─────────────────────────────────────────────────────────
//   Кэш-линия (обычно 64 байта) — минимальная единица когерентности. Каждая линия
//   в кэше ядра имеет состояние MESI:
//       M (Modified)  — линия изменена, валидна только в этом кэше;
//       E (Exclusive) — линия только здесь, совпадает с памятью;
//       S (Shared)    — линия может быть в нескольких кэшах, read-only;
//       I (Invalid)   — линия недействительна.
//   Когда ядро A пишет в линию, оно должно перевести её в M. Для этого протокол
//   рассылает Read-For-Ownership / Invalidate, и копии этой линии в кэшах других
//   ядер переходят в I (M->I у соседа). Если ядро B тут же пишет в СОСЕДНИЙ
//   счётчик ТОЙ ЖЕ линии, оно снова забирает линию себе в M, инвалидируя кэш A.
//   Линия безостановочно "пинг-понгует" между ядрами по шине когерентности
//   (cache ping-pong) — хотя логически потоки работают с РАЗНЫМИ переменными.
//   Это и есть false sharing: ложное разделение из-за гранулярности линии.
//
//   alignas(64) кладёт каждый счётчик на отдельную кэш-линию. Тогда запись ядра A
//   не затрагивает линию ядра B, линии спокойно живут в состоянии M у своего
//   владельца, трафика когерентности нет — масштабирование почти линейное.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q17_false_sharing/false_sharing_bench.cpp -o bin/q17_bench -lbenchmark -pthread
// run:     ./bin/q17_bench

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new> // std::hardware_destructive_interference_size

namespace {

// Размер кэш-линии. На большинстве x86-64 это 64 байта.
#ifdef __cpp_lib_hardware_interference_size
constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
constexpr std::size_t kCacheLine = 64;
#endif

constexpr int kMaxSlots = 8;

// Вариант 1: счётчики идут подряд -> почти наверняка на одной 64-байтной линии.
struct Shared {
    std::atomic<std::int64_t> slots[kMaxSlots];
};

// Вариант 2: каждый счётчик выровнен на отдельную кэш-линию.
struct alignas(kCacheLine) PaddedSlot {
    std::atomic<std::int64_t> value{0};
};

Shared     g_shared;
PaddedSlot g_padded[kMaxSlots];

} // namespace

// Каждый поток бьёт по своему слоту общей структуры — но все слоты на одной
// кэш-линии, поэтому ядра дерутся за неё (cache ping-pong).
static void BM_FalseSharing(benchmark::State& state) {
    const int slot = state.thread_index() % kMaxSlots;
    if (state.thread_index() == 0) {
        for (auto& counter : g_shared.slots) {
            counter.store(0, std::memory_order_relaxed);
        }
    }
    for (auto _ : state) {
        g_shared.slots[slot].fetch_add(1, std::memory_order_relaxed);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FalseSharing)->Threads(2)->Threads(4);

// Те же инкременты, но каждый счётчик на отдельной кэш-линии: трафика
// когерентности между ядрами нет.
static void BM_Padded(benchmark::State& state) {
    const int slot = state.thread_index() % kMaxSlots;
    if (state.thread_index() == 0) {
        for (auto& padded : g_padded) {
            padded.value.store(0, std::memory_order_relaxed);
        }
    }
    for (auto _ : state) {
        g_padded[slot].value.fetch_add(1, std::memory_order_relaxed);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Padded)->Threads(2)->Threads(4);

BENCHMARK_MAIN();
