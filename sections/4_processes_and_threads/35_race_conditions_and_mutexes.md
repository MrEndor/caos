##### Что такое race condition?

**Race condition (состояние гонки)** -- ситуация, когда два или более потока/процесса одновременно обращаются к общему
ресурсу (переменной, структуре данных), и результат выполнения зависит от **относительного порядка их выполнения**, что
приводит к **неопределённому поведению (UB)**.

Простой пример:

```
Поток 1:  x = 0
          x = x + 1      // читает x, прибавляет 1, пишет обратно
          
Поток 2:  x = x + 1      // одновременно то же самое
```

Возможные результаты:

- `x = 1` (если один прочитал и записал раньше, чем другой);
- `x = 1` (если оба прочитали 0, каждый добавил 1, неправильно!);
- `x = 2` (если правильно синхронизировано).

##### Приведите пример, когда возникает UB из-за одновременного изменения одних и тех же данных из разных тредов.

```cpp
#include <thread>
#include <vector>

int counter = 0;  // общая переменная

void increment() {
    for (int i = 0; i < 100000; ++i) {
        counter++;  // RACE CONDITION!
    }
}

int main() {
    std::vector<std::thread> threads;
    
    // Создать 10 потоков
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(increment);
    }
    
    // Подождать всех
    for (auto &t : threads) {
        t.join();
    }
    
    // counter должен быть 1000000, но обычно меньше!
    printf("Result: %d (expected 1000000)\n", counter);
    
    return 0;
}
```

Операция `counter++` **не атомарна**:

1. читает текущее значение;
2. прибавляет 1;
3. пишет обратно.

Между шагами может произойти переключение контекста, и два потока могут "потерять" изменения друг друга.

##### Что такое мьютекс?

**Mutex (Mutual Exclusion)** -- это примитив синхронизации, который обеспечивает **взаимное исключение**: только один
поток в данный момент может владеть мьютексом и входить в "критическую секцию".

Операции:

- `lock()` -- захватить мьютекс (заблокироваться, если занят);
- `unlock()` -- отпустить мьютекс.

Преимущества:

- предотвращает race conditions;
- гарантирует последовательный доступ к критическим ресурсам.

##### Приведите пример решения проблемы race condition с помощью мьютекса.

```cpp
#include <thread>
#include <mutex>
#include <vector>
#include <stdio.h>

int counter = 0;
std::mutex mtx;  // мьютекс для защиты counter

void increment() {
    for (int i = 0; i < 100000; ++i) {
        mtx.lock();             // входим в критическую секцию
        counter++;              // теперь безопасно
        mtx.unlock();           // выходим из критической секции
    }
}

int main() {
    std::vector<std::thread> threads;
    
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(increment);
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    printf("Result: %d (expected 1000000)\n", counter);
    // Вывод: Result: 1000000 ✓
    
    return 0;
}
```

**Более элегантно через RAII (lock_guard):**

```cpp
void increment() {
    for (int i = 0; i < 100000; ++i) {
        std::lock_guard<std::mutex> lock(mtx);  // автоматический unlock
        counter++;
    }
}
```

##### Что такое deadlock и как он может возникнуть?

**Deadlock (мёртвая блокировка)** -- ситуация, когда два или более потока ждут друг друга и ни один не может продолжить.

Пример:

```cpp
std::mutex mtx1, mtx2;

void thread1_func() {
    std::lock_guard<std::mutex> lock1(mtx1);  // захватил mtx1
    sleep(1);  // дать время потоку 2 захватить mtx2
    std::lock_guard<std::mutex> lock2(mtx2);  // ждёт mtx2... DEADLOCK!
}

void thread2_func() {
    std::lock_guard<std::mutex> lock2(mtx2);  // захватил mtx2
    sleep(1);
    std::lock_guard<std::mutex> lock1(mtx1);  // ждёт mtx1... DEADLOCK!
}
```

Оба потока захватывают по одному мьютексу и ждут второго. Никто не может продолжить.

**Как избежать:**

1. **Всегда захватывать мьютексы в одном и том же порядке:**

```cpp
void thread1_func() {
    std::lock_guard<std::mutex> lock1(mtx1);
    std::lock_guard<std::mutex> lock2(mtx2);
}

void thread2_func() {
    std::lock_guard<std::mutex> lock1(mtx1);  // тот же порядок!
    std::lock_guard<std::mutex> lock2(mtx2);
}
```

2. **Использовать `std::lock()` (atomically захватить несколько мьютексов):**

```cpp
std::lock(mtx1, mtx2);
std::lock_guard<std::mutex> lock1(mtx1, std::adopt_lock);
std::lock_guard<std::mutex> lock2(mtx2, std::adopt_lock);
```

3. **Установить timeout на захват:**

```cpp
if (mtx1.try_lock_for(std::chrono::milliseconds(100))) {
    // захватили, работаем
}
```

4. **Минимизировать количество одновременно захватываемых мьютексов**.

**Признаки deadlock'а:** программа вроде работает, но зависает (все потоки в "спящем" состоянии).
