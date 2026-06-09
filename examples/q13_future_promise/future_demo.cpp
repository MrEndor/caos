// Вопрос 13: Future / Promise — применение, callback hell, continuations, chaining, combining.
//
// Демонстрирует:
//   1. std::promise / std::future          — передача значения между потоками.
//   2. std::async                          — запуск асинхронной задачи.
//   3. std::packaged_task                  — обёртка вызываемого объекта во future.
//   4. callback hell (pyramid) vs futures  — три последовательных async-шага.
//   5. continuations / chaining            — самодельный then() (у std::future нет .then до C++23).
//   6. combining                           — самодельный when_all через несколько .get().
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q13_future_promise/future_demo.cpp -o bin/q13_future_promise -pthread
// run:     ./bin/q13_future_promise

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ───────────────────────── 1. promise / future ─────────────────────────
static void demo_promise_future() {
    std::cout << "\n[1] std::promise / std::future\n";

    std::promise<int> prom;
    std::future<int>  fut = prom.get_future();

    // producer-поток: посчитал результат и положил его в promise.
    std::thread producer([p = std::move(prom)]() mutable {
        std::this_thread::sleep_for(50ms);   // имитация работы
        p.set_value(42);
    });

    // consumer (main): .get() блокируется, пока producer не вызовет set_value.
    std::cout << "  consumer ждёт результат...\n";
    int result = fut.get();
    std::cout << "  получено: " << result << "\n";

    producer.join();
}

// ───────────────────────── 2. std::async ─────────────────────────
static void demo_async() {
    std::cout << "\n[2] std::async\n";

    // launch::async — гарантированно отдельный поток.
    std::future<long> fut = std::async(std::launch::async, [] {
        long sum = 0;
        for (long i = 1; i <= 1000; ++i) {
            sum += i;
        }
        return sum;
    });

    std::cout << "  сумма 1..1000 = " << fut.get() << "\n";
}

// ───────────────────────── 3. std::packaged_task ─────────────────────────
static void demo_packaged_task() {
    std::cout << "\n[3] std::packaged_task\n";

    // packaged_task оборачивает вызываемый объект; его future получает результат вызова.
    std::packaged_task<int(int, int)> task([](int lhs, int rhs) {
        return lhs * rhs;
    });
    std::future<int> fut = task.get_future();

    // Задачу можно передать в поток / пул и выполнить позже.
    std::thread worker(std::move(task), 6, 7);
    std::cout << "  6 * 7 = " << fut.get() << "\n";
    worker.join();
}

// ───────────────────────── 4. callback hell vs futures ─────────────────────────

// Три «асинхронных» шага, выраженных через callback (каждый принимает продолжение).
static void fetch_cb(std::function<void(int)> cont) {
    std::thread([cont = std::move(cont)] {
        std::this_thread::sleep_for(10ms);
        cont(100);                       // «загрузили» 100
    }).detach();
}
static void process_cb(int value, std::function<void(int)> cont) {
    std::thread([value, cont = std::move(cont)] {
        std::this_thread::sleep_for(10ms);
        cont(value * 2);                 // обработали
    }).detach();
}
static void store_cb(int value, std::function<void(int)> cont) {
    std::thread([value, cont = std::move(cont)] {
        std::this_thread::sleep_for(10ms);
        cont(value + 1);                 // сохранили
    }).detach();
}

static void demo_callback_hell() {
    std::cout << "\n[4] callback hell (pyramid of doom)\n";

    std::promise<int> done;
    std::future<int>  done_fut = done.get_future();

    // Классическая «пирамида»: вложенность растёт с каждым шагом,
    // обработку ошибок и поток управления отследить тяжело.
    fetch_cb([&done](int fetched) {
        process_cb(fetched, [&done](int processed) {
            store_cb(processed, [&done](int stored) {
                done.set_value(stored);
            });
        });
    });

    std::cout << "  результат pipeline (callbacks) = " << done_fut.get() << "\n";
}

// ───────────────────────── 5. continuations / chaining: самодельный then() ─────────────────────────

// then(future, f): запускает новый async, который ждёт предыдущий future
// и применяет к его результату f. Возвращает future с результатом f.
// Это упрощённая версия .then() из C++23 / std::experimental.
template <typename T, typename Func>
auto then(std::future<T> fut, Func func) -> std::future<std::invoke_result_t<Func, T>> {
    return std::async(std::launch::async,
                      [fut = std::move(fut), func = std::move(func)]() mutable {
                          return func(fut.get());   // дождались предыдущего и трансформировали
                      });
}

static void demo_chaining() {
    std::cout << "\n[5] continuations / chaining: fetch().then(parse).then(format)\n";

    auto fetch  = [] {
        std::this_thread::sleep_for(10ms);
        return std::string("12345");
    };
    auto parse  = [](const std::string& text) { return std::stoi(text); };
    auto format = [](int number) { return "value=" + std::to_string(number); };

    // Тот же pipeline, что в [4], но плоский и читаемый — без вложенности.
    std::future<std::string> pipeline =
        then(then(std::async(std::launch::async, fetch), parse), format);

    std::cout << "  результат chain = " << pipeline.get() << "\n";
}

// ───────────────────────── 6. combining: самодельный when_all ─────────────────────────

// when_all: дожидается всех future и собирает их результаты в вектор.
template <typename T>
static std::vector<T> when_all(std::vector<std::future<T>>& futures) {
    std::vector<T> results;
    results.reserve(futures.size());
    for (auto& future : futures) {
        results.push_back(future.get());   // .get() по очереди = ждём всех
    }
    return results;
}

static void demo_combining() {
    std::cout << "\n[6] combining: when_all\n";

    std::vector<std::future<int>> futures;
    for (int i = 1; i <= 4; ++i) {
        futures.push_back(std::async(std::launch::async, [i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10 * i));
            return i * i;                  // параллельно считаем квадраты
        }));
    }

    std::vector<int> results = when_all(futures);
    std::cout << "  квадраты 1..4 = ";
    for (int square : results) {
        std::cout << square << ' ';
    }
    std::cout << "\n";
}

int main() {
    std::cout << "=== Вопрос 13: Future / Promise ===\n";
    demo_promise_future();
    demo_async();
    demo_packaged_task();
    demo_callback_hell();
    demo_chaining();
    demo_combining();
    std::cout << "\n=== готово ===\n";
    return 0;
}
