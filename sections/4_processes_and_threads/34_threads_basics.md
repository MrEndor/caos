##### Что такое потоки выполнения (треды, threads, нити)?

**Поток выполнения (thread)** -- это легковесный процесс в контексте одного процесса. В отличие от процессов, потоки 
**одного процесса** делят:

- одно адресное пространство (обычную память, .data, .bss, heap)
- открытые файловые дескрипторы
- текущую директорию, umask, сигнал-маски (в некоторых случаях)

Но каждый поток имеет свой:

- стек (TLS, thread-local storage)
- счётчик инструкций (program counter)
- регистры процессора
- ID потока (tid)

Потоки используются для:

- параллельной обработки данных на многоядерных системах
- пока один поток ждёт (I/O), другой может выполнять работу
- более лёгкие переключения контекста, чем между процессами

**Архитектура процесса с потоками:**

```mermaid
graph TB
    subgraph Process["Процесс (общие ресурсы)"]
        Memory["Адресное пространство<br/>(.text, .data, .bss, heap)"]
        FD["Открытые файловые<br/>дескрипторы"]
        Dir["Текущая директория<br/>и другие параметры"]
    end

    subgraph Threads["Потоки (уникальные)"]
        T1["Поток 1<br/>Стек, регистры, tid=1001"]
        T2["Поток 2<br/>Стек, регистры, tid=1002"]
        T3["Поток 3<br/>Стек, регистры, tid=1003"]
    end

    Memory -.->|общая| T1
    Memory -.->|общая| T2
    Memory -.->|общая| T3
    FD -.->|общие| T1
    FD -.->|общие| T2
    FD -.->|общие| T3
    Dir -.->|общие| T1
    Dir -.->|общие| T2
    Dir -.->|общие| T3
```

##### Покажите пример создания и использования thread на С++.

В C++11 и выше используется `<thread>`:

```cpp
#include <thread>
#include <iostream>

void worker(int id) {
    for (int i = 0; i < 5; ++i) {
        std::cout << "Thread " << id << ": iteration " << i << "\n";
    }
}

int main() {
    // Создаём поток, который выполняет функцию worker(1)
    std::thread t1(worker, 1);
    
    // Создаём второй поток
    std::thread t2(worker, 2);
    
    // Основной поток может продолжить свою работу
    std::cout << "Main thread continues...\n";
    
    // Дожидаемся завершения обоих потоков
    t1.join();
    t2.join();
    
    std::cout << "All threads finished\n";
    return 0;
}
```

Компиляция:

```bash
g++ -std=c++11 -pthread thread_example.cpp -o thread_example
./thread_example
```

##### Покажите пример параллельной обработки из двух тредов каких-либо данных.

Пример: два потока обрабатывают элементы вектора и суммируют их:

```cpp
#include <thread>
#include <vector>
#include <iostream>
#include <mutex>

std::mutex result_mutex;
long long total = 0;

void sum_range(const std::vector<int>& data, int start, int end) {
    long long partial_sum = 0;
    
    // Каждый поток считает свою часть БЕЗ блокировок (быстро)
    for (int i = start; i < end; ++i) {
        partial_sum += data[i];
    }
    
    // Только для добавления результата нужен мьютекс
    {
        std::lock_guard<std::mutex> lock(result_mutex);
        total += partial_sum;
    }
}

int main() {
    std::vector<int> data(1000);
    for (int i = 0; i < 1000; ++i) {
        data[i] = i + 1;
    }
    
    // Первый поток обрабатывает элементы 0-499
    std::thread t1(sum_range, std::ref(data), 0, 500);
    
    // Второй поток обрабатывает элементы 500-999
    std::thread t2(sum_range, std::ref(data), 500, 1000);
    
    t1.join();
    t2.join();
    
    std::cout << "Total sum: " << total << "\n";  // 500500
    return 0;
}
```

Здесь оба потока работают параллельно, каждый обрабатывает свою половину данных.

##### Что делают методы join и detach?

**`join()`:**

- Блокирует вызывающий поток до тех пор, пока целевой поток **не завершится**
- Ожидает всех наработок целевого потока
- После `join` можно проверить результаты работы потока
- Эта операция может быть вызвана только один раз

```cpp
std::thread t(worker);
t.join();  // Ждём завершения потока
std::cout << "Thread finished\n";
```

**`detach()`:**

- Отсоединяет поток от объекта `std::thread`
- Поток продолжает выполняться в фоне
- Вызывающий поток **не ждёт** завершения целевого потока
- После `detach()` объект `std::thread` больше нельзя контролировать

```cpp
std::thread t(worker);
t.detach();  // Отпускаем поток
std::cout << "Thread is running in background\n";
// Основной поток может завершиться, но фоновый поток продолжит работу
```

**Жизненный цикл потока - диаграмма:**

```mermaid
stateDiagram-v2
    [*] --> Created: thread объект<br/>создан
    Created --> Running: поток начал<br/>выполнение
    Running --> WaitingForJoin: поток закончил<br/>работу, ждёт join()
    Running --> Background: вызвана<br/>detach()
    WaitingForJoin --> Destroyed: вызвана<br/>join()
    Background --> Destroyed: поток<br/>завершился
    Destroyed --> [*]
    Created --> Error: ~thread()<br/>без join/detach
    Error --> [*]
```

**Сравнение:**

| Операция                     | join       | detach                     |
|------------------------------|------------|----------------------------|
| Ожидание завершения          | Да         | Нет                        |
| Контроль потока              | Есть       | Нет                        |
| Возможность повторно вызвать | Нет        | Нет                        |
| Когда очищаются ресурсы      | После join | Когда поток сам завершится |

##### Что происходит, если main завершается, но при этом еще не все треды завершили свою работу?

Если вы вызвали `detach()`:

- Поток продолжает работать, но часто ведёт себя непредсказуемо
- Поток может обращаться к памяти, которая уже была освобождена (деструкторы глобальных объектов)
- Приводит к **undefined behavior** и часто крашам

Если вы забыли `join()`:

- Объект `std::thread` вызовет `std::terminate()` при своём уничтожении (в деструкторе)
- Программа **упадёт** с сообщением об ошибке

**Сценарии завершения программы:**

```mermaid
graph TD
    Main["main() завершается"]
    Main --> Q1{"join() вызван?"}
    Q1 -->|Да| Safe["✓ Безопасно<br/>все потоки дождались"]
    Q1 -->|Нет| Q2{"detach() вызван?"}
    Q2 -->|Да| UB["⚠ Undefined behavior<br/>потоки в фоне, доступ<br/>к уничтоженной памяти"]
    Q2 -->|Нет| Crash["❌ CRASH<br/>std::terminate()"]
    Safe --> Exit["Program exit"]
    UB --> Exit
    Crash --> Exit
```

**Пример плохого кода:**

```cpp
int main() {
    std::thread t(worker);
    // Забыли join или detach!
    return 0;  // Крах: деструктор ~thread вызовет std::terminate()
}
```

**Правильный код:**

```cpp
int main() {
    std::thread t(worker);
    t.join();  // Или t.detach()
    return 0;
}
```

Для гарантии можно использовать RAII-паттерн:

```cpp
class ThreadGuard {
    std::thread& t;
public:
    explicit ThreadGuard(std::thread& t_) : t(t_) {}
    ~ThreadGuard() {
        if (t.joinable()) {
            t.join();
        }
    }
};

int main() {
    std::thread t(worker);
    ThreadGuard guard(t);  // Гарантирует join при выходе
    // ... код ...
    return 0;  // guard вызовет join в деструкторе
}
```

**Правильный RAII паттерн - диаграмма:**

```mermaid
graph LR
    A["Создание потока<br/>std::thread t(...)"]
    B["Создание guard<br/>ThreadGuard g(t)"]
    C["Выполнение кода"]
    D["Выход из scope"]
    E["~ThreadGuard вызывает<br/>t.join()"]
    F["Поток дождался<br/>и завершился"]
    G["Безопасное завершение<br/>программы"]
    A --> B
    B --> C
    C --> D
    D --> E
    E --> F
    F --> G
```
