// Вопрос 21: модель памяти C++, store buffering, барьеры, mfence.
//
// Демонстрирует ЭМПИРИЧЕСКИ перестановку Store->Load (store buffering / litmus
// Dekker) и то, как её убирает seq_cst-барьер:
//   T1: x=1; r1=y;     T2: y=1; r2=x;     (x,y изначально 0)
//   "Невозможный" по последовательной логике исход r1==0 && r2==0 РЕАЛЬНО
//   случается на x86, потому что запись оседает в store buffer ядра и ещё не видна
//   другому ядру, когда тот делает load.
// Гоняем много итераций, считаем число таких исходов БЕЗ барьера и С барьером.
//
// ── Модель памяти кратко ──────────────────────────────────────────────────────
//   sequenced-before    — порядок внутри одного потока (что раньше в программе).
//   synchronizes-with   — release-store синхронизируется с acquire-load той же
//                         переменной, прочитавшим записанное значение.
//   happens-before      — транзитивное замыкание sequenced-before и
//                         synchronizes-with; гарантирует видимость записей.
//
// ── Почему x86 (TSO) переставляет Store->Load ─────────────────────────────────
//   У x86 модель Total Store Order: каждое ядро имеет store buffer. Запись сначала
//   попадает в свой буфер (быстро), а в когерентный кэш «сливается» позже. Свои
//   записи ядро видит через store-to-load forwarding, но ЧУЖОЕ ядро их пока не
//   видит. Поэтому load, идущий ПОСЛЕ store в программе, может выполниться раньше,
//   чем store станет глобально видимым — это и есть переупорядочивание Store->Load
//   (единственное, что TSO допускает; Load->Load, Store->Store, Load->Store — нет).
//
// ── Как убрать ────────────────────────────────────────────────────────────────
//   MFENCE / locked RMW (lock xchg, lock add ...) / seq_cst store «дренируют»
//   store buffer: запись становится глобально видимой до следующего load.
//   В C++ это даёт std::memory_order_seq_cst или std::atomic_thread_fence(seq_cst)
//   между store и load. Тогда исход r1==0 && r2==0 СТАНОВИТСЯ НЕВОЗМОЖНЫМ.
//   Разные memory_order: relaxed (только атомарность), acquire/release (одна
//   синхронизация-пара, Store->Load НЕ запрещён), seq_cst (единый глобальный
//   порядок всех seq_cst-операций — даёт полный барьер).
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q21_memory_model/store_buffering_test.cpp -o bin/q21_test -lgtest -lgtest_main -pthread
// run:     ./bin/q21_test

#include <atomic>
#include <string>
#include <thread>

#include <gtest/gtest.h>

// Общее состояние одного прогона litmus-теста.
struct SB {
    std::atomic<int> x{0}, y{0};
    std::atomic<int> r1{0}, r2{0};
    // Барьер «начала итерации»: оба потока крутятся, пока gen не станет нужным.
    std::atomic<int> gen{0};
    // Счётчики «дошёл до барьера» / «закончил тело» для синхронизации с драйвером.
    std::atomic<int> arrived{0};
    std::atomic<int> finished{0};
};

// Ядро litmus-теста: один поток исполняет свою половину Dekker.
// fence=true вставляет seq_cst-барьер между store и load (дренаж store buffer).
// НЕ менять нотацию x/y/r1/r2 и memory_order — это и есть проверяемый инвариант.
static void litmus_body(SB& state, bool first, bool fence) {
    if (first) {
        state.x.store(1, std::memory_order_relaxed);
        if (fence) {
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }
        state.r1.store(state.y.load(std::memory_order_relaxed), std::memory_order_relaxed);
    } else {
        state.y.store(1, std::memory_order_relaxed);
        if (fence) {
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }
        state.r2.store(state.x.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}

// Поток-воркер: на каждой итерации сигналит готовность, ждёт старта от драйвера,
// исполняет litmus-тело и отмечает завершение.
static void litmus_worker(SB& state, bool first, bool fence, int iters) {
    for (int i = 1; i <= iters; ++i) {
        state.arrived.fetch_add(1, std::memory_order_acq_rel);   // «я готов к итерации i»
        while (state.gen.load(std::memory_order_acquire) != i) {
            /* spin до разрешения */
        }
        litmus_body(state, first, fence);
        state.finished.fetch_add(1, std::memory_order_acq_rel); // «тело итерации выполнено»
    }
}

// Координация одной итерации со стороны драйвера: дождаться обоих у барьера,
// обнулить x/y, дать старт и собрать исход. true, если случился r1==0 && r2==0.
static bool drive_iteration(SB& state, int i) {
    // Ждём, пока оба потока дойдут до барьера итерации i.
    while (state.arrived.load(std::memory_order_acquire) != 2 * i) {
        /* spin */
    }
    state.x.store(0, std::memory_order_relaxed);
    state.y.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);     // обнуление видно обоим
    state.gen.store(i, std::memory_order_release);           // СТАРТ итерации i
    // Ждём, пока оба выполнят тело.
    while (state.finished.load(std::memory_order_acquire) != 2 * i) {
        /* spin */
    }
    return state.r1.load(std::memory_order_relaxed) == 0 &&
           state.r2.load(std::memory_order_relaxed) == 0;
}

// fence=false -> relaxed (Store->Load переставляется);
// fence=true  -> seq_cst-барьер между store и load (дренаж store buffer).
// Возвращает количество «запрещённых» исходов r1==0 && r2==0.
static long long run_sb(bool fence, int iters) {
    SB state;
    long long forbidden = 0;

    std::thread thread_first(litmus_worker, std::ref(state), true, fence, iters);
    std::thread thread_second(litmus_worker, std::ref(state), false, fence, iters);

    for (int i = 1; i <= iters; ++i) {
        if (drive_iteration(state, i)) {
            ++forbidden;
        }
    }

    thread_first.join();
    thread_second.join();
    return forbidden;
}

// ───────────────────────── тесты ─────────────────────────

// seq_cst-fence дренирует store buffer: исход r1==0 && r2==0 СТАНОВИТСЯ
// невозможным — это детерминированный инвариант модели памяти.
TEST(MemoryModel, SeqCstFenceForbidsReordering) {
    constexpr int kIters = 200'000;

    long long forbidden = run_sb(/*fence=*/true, kIters);
    EXPECT_EQ(forbidden, 0);

    // Информационно (НЕ assert): relaxed-прогон обычно показывает reordering > 0,
    // но это недетерминированно и может быть 0 на слабой нагрузке — поэтому только
    // выводим как property, а критерий успеха — лишь по барьеру.
    long long relaxed_reorder = run_sb(/*fence=*/false, kIters);
    RecordProperty("relaxed_store_load_reorder", std::to_string(relaxed_reorder));
    SCOPED_TRACE("relaxed Store->Load reorder count (informational): " +
                 std::to_string(relaxed_reorder));
}

// acquire/release: producer release-store флага ПОСЛЕ записи данных, consumer
// acquire-load флага -> гарантированно видит данные (happens-before).
TEST(MemoryModel, AcquireReleaseHappensBefore) {
    std::atomic<bool> ready{false};
    int data = 0;                       // ОБЫЧНАЯ (неатомарная) переменная

    std::thread producer([&] {
        data = 42;                                       // (1) запись данных
        ready.store(true, std::memory_order_release);    // (2) публикуем флаг
    });

    int observed = -1;
    std::thread consumer([&] {
        while (!ready.load(std::memory_order_acquire)) {
            /* spin */                                   // (3)
        }
        observed = data;   // (4) гарантированно видим 42: (1) happens-before (4)
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(observed, 42);
}
