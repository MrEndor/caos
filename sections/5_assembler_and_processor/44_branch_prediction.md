##### Что такое branch prediction, в чем его идея?

**Branch Prediction** -- это механизм процессора, который **предсказывает результат условного перехода (if/else)** до
его вычисления, чтобы не прерывать pipeline выполнения инструкций.

**Проблема без prediction:**

- При условном переходе (je, jne и т.д.) процессор не знает, какую ветку выполнять;
- Он должен дождаться результата (flush pipeline);
- Пенальти: 10-20 тактов простоя.

**С branch prediction:**

- Процессор **угадывает**, какая ветка вероятнее;
- Продолжает выполнять инструкции "по угадке";
- Если угадал правильно -- никакого штрафа;
- Если ошибся -- flush pipeline, откат (mispredict penalty: 15-20 тактов).

**Статистика:**

- Современные процессоры предсказывают ~95% переходов;
- Неправильное предсказание очень дорого.

##### Покажите эксперимент, доказывающий существование данного явления.

```cpp
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

int main() {
    // Большой массив случайных чисел
    constexpr int SIZE = 32768;
    std::vector<int> data(SIZE);

    // Инициализировать случайно
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int& val : data) {
        val = dist(rng);
    }

    using Clock = std::chrono::steady_clock;

    // ТЕСТ 1: Неотсортированные данные (плохо предсказывается)
    auto start = Clock::now();

    long sum = 0;
    for (int i = 0; i < SIZE; ++i) {
        if (data[i] >= 128) {  // случайный условный переход
            sum += data[i];
        }
    }

    auto end = Clock::now();
    auto time_unsorted = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Unsorted:  " << time_unsorted << " ns  (sum=" << sum << ")\n";

    // Отсортировать (хорошо предсказывается)
    std::sort(data.begin(), data.end());

    // ТЕСТ 2: Отсортированные данные (хорошо предсказывается)
    start = Clock::now();

    sum = 0;
    for (int i = 0; i < SIZE; ++i) {
        if (data[i] >= 128) {  // ТОЖЕ условие, но данные отсортированы
            sum += data[i];
        }
    }

    end = Clock::now();
    auto time_sorted = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Sorted:    " << time_sorted << " ns  (sum=" << sum << ")\n";
    std::cout << "Speedup:   " << static_cast<double>(time_unsorted) / time_sorted << "x\n";
}
```

**Ожидаемый результат:**

```
Unsorted:  1200 ns  (sum=...)
Sorted:     400 ns  (sum=...)
Speedup:   3.0x
```

**Объяснение:**

- При неотсортированных данных branch predict'ор не может предсказать переход -> много mispredict'ов;
- При отсортированных данных первая половина пропускает блок, вторая выполняет -> легко предсказывается;
- Разница: 3x ускорение только благодаря branch prediction'у!

##### Как подсказать компилятору, какая из веток if более вероятна?

**C++20: [[likely]] и [[unlikely]]:**

```cpp
if (data[i] >= 128) [[likely]] {
    sum += data[i];
}

if (error) [[unlikely]] {
    handle_error();
}
```

**GCC/Clang (старый способ):**

```c
if (__builtin_expect(data[i] >= 128, 1)) {  // 1 = true вероятнее
    sum += data[i];
}

if (__builtin_expect(error, 0)) {  // 0 = false вероятнее
    handle_error();
}
```

**Макрос для удобства:**

```c
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

if (likely(data[i] >= 128)) {
    sum += data[i];
}
```

##### Как это отразится на ассемблерном коде?

```asm
# БЕЗ подсказки (или с [[unlikely]]):
cmp %eax, $128
jl skip           # прыгнуть на skip (default)
add %eax, %sum
skip:

# С [[likely]]:
cmp %eax, $128
jge add_to_sum    # прыгнуть на add_to_sum (likely)
jmp skip
add_to_sum:
add %eax, %sum
skip:
```

**Различие:**

- **Без подсказки:** код для вероятной ветки после jump -> если branch predict'ор ошибётся, prefetch был не туда;
- **С подсказкой:** код для вероятной ветки идёт сразу -> даже если ошибётся, хоть какой-то код был prefetch'ен
  правильно.

**Практическое использование:**

- Error handling: `if (unlikely(error)) { ... }`
- Hot paths: `if (likely(common_case)) { ... }`
- Может дать 5-10% ускорения в критичных местах.
