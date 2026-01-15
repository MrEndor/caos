##### Кэши процессора. Сколько уровней кэша есть в процессоре, зачем они нужны?

**CPU Cache** -- это быстрая память между процессором и RAM:

- **L1** -- 32KB (per core), задержка ~4 такта;
- **L2** -- 256KB (per core), задержка ~12 тактов;
- **L3** -- 8-20MB (shared между cores), задержка ~40 тактов;
- **RAM** -- задержка ~100-200 тактов.

**Зачем нужны:**

- RAM очень медленна, но велика;
- Кэши маленькие, но быстрые;
- Программы часто обращаются к близким адресам (locality of reference);
- Кэш ускоряет доступ в 10-50 раз.

**Иерархия (Memory Hierarchy):**

```
   ┌──────────────────┐
   │ CPU Core         │
   │  (2-3 ГГц)       │
   └────────┬─────────┘
            │
   ┌────────▼─────────┐
   │ L1 Cache (32KB)  │ 4 такта
   │ (per core)       │
   └────────┬─────────┘
            │
   ┌────────▼─────────┐
   │ L2 Cache (256KB) │ 12 тактов
   │ (per core)       │
   └────────┬─────────┘
            │
   ┌────────▼──────────┐
   │ L3 Cache (8-20MB) │ 40 тактов
   │ (shared)          │
   └────────┬──────────┘
            │
   ┌────────▼──────────────┐
   │ RAM (8-64GB)          │ 100-200 тактов
   └───────────────────────┘
```

##### Как узнать размеры кэшей своего процессора?

```bash
lscpu | grep -i cache           # информация о кэшах
cat /proc/cpuinfo               # в том числе cache_alignment
getconf LEVEL1_DCACHE_SIZE      # L1 data cache
getconf LEVEL2_CACHE_SIZE       # L2 cache
getconf LEVEL3_CACHE_SIZE       # L3 cache
```

**Пример вывода:**

```
L1d cache:                       192 KiB (6 instances)
L1i cache:                       192 KiB (6 instances)
L2 cache:                        1,5 MiB (6 instances)
L3 cache:                        12 MiB (1 instance)
```

##### Что такое кэш-линия?

**Cache Line** -- это минимальный блок памяти, который **атомарно** передаётся между кэшем и RAM. Обычно **64\128 байта
** (
на современных Intel/AMD процессорах).

**Важность:**

- когда процессор обращается к одному байту в памяти, загружается вся кэш-линия (64 байта);
- если следующее обращение попадает в ту же кэш-линию, это очень быстро;
- если нет -> cache miss -> перезагрузка;
- это объясняет, почему обход массива по порядку быстрее, чем в случайном порядке.

```bash
getconf LEVEL1_DCACHE_LINESIZE   # обычно 64 байта
```

##### Почему делать обход матрицы по строкам эффективнее, чем по столбцам?

**По строкам (cache-friendly):**

```c
int matrix[1000][1000];

for (int i = 0; i < 1000; i++)           // столбцы
    for (int j = 0; j < 1000; j++)       // строки
        sum += matrix[i][j];             // БЫСТРО
```

**Элементы расположены в памяти подряд**: matrix[0][0], matrix[0][1], ..., matrix[0][63], ...

При обращении к matrix[0][0] загружается вся кэш-линия (64 байта), содержащая первые 16 элементов (64/4). Следующие 15
обращений попадают в кэш!

**По столбцам (cache-unfriendly):**

```c
for (int j = 0; j < 1000; j++)           // строки
    for (int i = 0; i < 1000; i++)       // столбцы
        sum += matrix[i][j];             // МЕДЛЕННО
```

**Элементы расположены не подряд**: matrix[0][0], matrix[1][0], matrix[2][0], ...

Каждое обращение может быть в другой кэш-линии -> много cache miss'ов.

**Разница в скорости: 10-50x!**

##### Покажите эксперимент, доказывающий, что кэш-линии существуют.

```cpp
#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>

constexpr size_t ARRAY_SIZE = 100'000'000;

int main() {
    for (int64_t step = 1; step < 1024; step *= 2) {
        std::vector<int> array(ARRAY_SIZE, 0);

        auto start = std::chrono::steady_clock::now();

        int sum = 0;
        for (size_t i = 0; i < ARRAY_SIZE; i += step) {
            sum += array[i];
        }

        auto end = std::chrono::steady_clock::now();

        auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        double per_iter = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed_ms).count() / (ARRAY_SIZE / step);

        std::cout << "STEP=" << step
                  << ", Time=" << elapsed_ms.count() << " ms"
                  << ", per iteration=" << per_iter << " ns\n";
    }
}
```

**Ожидаемый результат:**

```
STEP=1,   Time=... ns, per iteration=0.1 ns  (все в L1)
STEP=2,   Time=... ns, per iteration=0.1 ns  (всё ещё в L1)
...
STEP=16,  Time=... ns, per iteration=0.5 ns  (на границе L1)
STEP=32,  Time=... ns, per iteration=1.0 ns  (в L2)
STEP=64,  Time=... ns, per iteration=5.0 ns  (cache miss, RAM)
STEP=256, Time=... ns, per iteration=... ns  (совсем медленно)
```

Кэш-линия -- 64 байта = 16 int'ов. При STEP=64 (256 байт) элементы отстоят на 4 кэш-линии → много miss'ов.

##### Покажите эксперимент, доказывающий, что существуют кэши разных уровней.

```cpp
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

using Clock = std::chrono::steady_clock;

int main() {
    std::vector<std::pair<std::size_t, const char*>> levels = {
        {32 * 1024,        "L1"},   // 32 KB
        {256 * 1024,       "L2"},   // 256 KB
        {12 * 1024 * 1024, "L3"},   // 12 MB
        {64 * 1024 * 1024, "RAM"}   // 64 MB
    };

    constexpr std::size_t TOTAL_ACCESSES = 100'000;
    constexpr int REPEATS = 50;
    constexpr size_t PAGE_SIZE = 4096;
    constexpr size_t STRIDE_BYTES = PAGE_SIZE;

    std::mt19937_64 rng{std::random_device{}()};

    for (const auto& [size_bytes, name] : levels) {
        const std::size_t elem_count = size_bytes / sizeof(int);
        const std::size_t stride = STRIDE_BYTES / sizeof(int);

        if (stride == 0 || elem_count < stride) {
            std::cout << name << " — слишком мал для stride\n";
            continue;
        }

        std::size_t accesses_per_pass = elem_count / stride;

        std::size_t passes = (TOTAL_ACCESSES + accesses_per_pass - 1) / accesses_per_pass;

        std::vector<int> array(elem_count, 1);

        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < elem_count; i += stride) {
            indices.push_back(i);
        }

        volatile int warmup = 0;
        for (int w = 0; w < 10; ++w) {
            std::shuffle(indices.begin(), indices.end(), rng);
            for (std::size_t idx : indices) {
                warmup += array[idx];
            }
        }

        long double total_time_ns = 0;
        volatile int sink = 0;

        for (int rep = 0; rep < REPEATS; ++rep) {
            std::shuffle(indices.begin(), indices.end(), rng);

            auto start = Clock::now();

            for (std::size_t pass = 0; pass < passes; ++pass) {
                int sum = 0;
                for (std::size_t idx : indices) {
                    sum += array[idx];
                }
                sink += sum;
            }

            auto end = Clock::now();
            total_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count();
        }

        auto avg_time_ns = total_time_ns / REPEATS;
        auto per_access_ns = avg_time_ns / TOTAL_ACCESSES;

        std::cout << name << " (" << size_bytes / 1024.0 / 1024.0 << " MB)"
                  << ", stride=" << STRIDE_BYTES << "B"
                  << ", passes=" << passes
                  << ", accesses=" << TOTAL_ACCESSES
                  << ", avg=" << avg_time_ns / 1e6 << " ms"
                  << ", per access=" << per_access_ns << " ns\n";
    }
}
```

**Результат показывает скачки задержки на границах L1 -> L2 -> L3 -> RAM.**
