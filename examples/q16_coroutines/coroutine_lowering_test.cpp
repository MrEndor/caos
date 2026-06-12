// Вопрос 16 (lowering): во что компилятор разворачивает co_await / co_return.
//
// Исполняемая версия раздела «Что компилятор делает с coroutine» из вики
// (sections/concurrency/coroutines_cpp20.md). Имена, состояния и структура
// специально те же, что в вики: корутина fetch_and_log, awaiter'ы async_read /
// async_write, локалки data / doubled, кадр fetch_and_log_frame, resume-функция
// fetch_and_log_resume со switch по fsm_state. Слева — настоящая корутина,
// справа — написанный РУКАМИ тот же state machine; тест проверяет, что они дают
// один результат за одинаковое число resume (точек приостановки) и одинаково
// исполняют прямой код между co_await.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q16_coroutines/coroutine_lowering_test.cpp -o bin/q16_lowering_test -lgtest -lgtest_main -pthread
// run:     ./bin/q16_lowering_test

#include <coroutine>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// «Лог» побочного эффекта — это прямой код между двумя co_await (см. вики:
// log("got value")). Считаем, сколько раз он реально исполнился.
static std::vector<std::string> g_log;
static void log_line(const char* message) { g_log.emplace_back(message); }

// ───────────────────────── awaiter'ы (как в вики) ─────────────────────────
// await_ready()==false -> корутина гарантированно приостанавливается на каждом
// co_await, и каждая точка становится наблюдаемой (один resume = один шаг).
// Эти ЖЕ типы используются и в настоящей корутине, и в ручном автомате.
struct async_read_awaiter {
    int id;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}  // отдать управление caller'у
    int  await_resume() const noexcept { return id + 100; }        // «прочитанное» значение
};

struct async_write_awaiter {
    int value;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

static async_read_awaiter  async_read(int id)     noexcept { return {id}; }
static async_write_awaiter async_write(int value) noexcept { return {value}; }

// ═════════════════════════ 1) настоящая корутина ═════════════════════════
template <typename T>
struct Task {
    struct promise_type {
        T result{};
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        void return_value(T value) noexcept { result = std::move(value); }
        void unhandled_exception()           { std::terminate(); }
    };

    std::coroutine_handle<promise_type> handle;
    explicit Task(std::coroutine_handle<promise_type> coro) : handle(coro) {}
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, {})) {}
    ~Task() {
        if (handle) {
            handle.destroy();
        }
    }
};

// Та самая корутина из вики — её и разворачиваем руками ниже.
static Task<int> fetch_and_log(int id) {
    int data = co_await async_read(id);     // (A) suspend point #1
    log_line("got value");                  // обычный код между await'ами
    int doubled = data * 2;
    co_await async_write(doubled);          // (B) suspend point #2
    co_return doubled;
}

// ═════════════════════════ 2) ручной state machine ═════════════════════════
// Состояния — точки приостановки (фактически instruction pointer). Имена как в
// вики; INITIAL_SUSPEND и SUSPENDED_ON_x — логические (припаркованы), в рабочем
// switch не нужны: на suspend во фрейме хранится уже AFTER_x — точка, куда
// switch прыгнет при следующем resume().
enum class fsm_state {
    BEFORE_READ, AFTER_READ, BEFORE_WRITE, AFTER_WRITE, FINAL_SUSPEND, DONE
};

// Coroutine frame: всё, что должно пережить приостановку. В настоящей корутине
// это выделяется на куче; компилятор кладёт сюда промис, параметры, локалки,
// живущие через co_await, временные awaiter'ы и текущее состояние FSM.
struct fetch_and_log_frame {
    struct promise_type {
        int result = 0;
        void return_value(int value) noexcept { result = value; }
    } promise;

    int id = 0;                              // параметр, скопирован в ramp
    int data = 0;                            // живёт через co_await -> поле фрейма
    int doubled = 0;                         // живёт через co_await -> поле фрейма
    async_read_awaiter  read_aw{0};          // awaiter'ы хранятся во фрейме до await_resume
    async_write_awaiter write_aw{0};         // (в реальности — один union на шаг)
    fsm_state state = fsm_state::BEFORE_READ;// INITIAL_SUSPEND уже пройден в ramp

    bool done() const noexcept { return state == fsm_state::DONE; }
};

// Resume function — switch по состояниям. Та же структура, что в вики.
static void fetch_and_log_resume(fetch_and_log_frame* frame) {
    switch (frame->state) {
    case fsm_state::BEFORE_READ:
        frame->read_aw = async_read(frame->id);          // data = co_await async_read(id);
        if (!frame->read_aw.await_ready()) {
            frame->state = fsm_state::AFTER_READ;        // куда вернётся resume()
            frame->read_aw.await_suspend({});
            return;                                      // <- suspend: возврат в caller resume()
        }
        [[fallthrough]];                                 // await_ready==true -> без приостановки
    case fsm_state::AFTER_READ:
        frame->data = frame->read_aw.await_resume();     // значение из co_await (A)
        log_line("got value");                           // прямой код между co_await
        frame->doubled = frame->data * 2;
        [[fallthrough]];
    case fsm_state::BEFORE_WRITE:
        frame->write_aw = async_write(frame->doubled);   // co_await async_write(doubled);
        if (!frame->write_aw.await_ready()) {
            frame->state = fsm_state::AFTER_WRITE;
            frame->write_aw.await_suspend({});
            return;                                      // <- suspend
        }
        [[fallthrough]];
    case fsm_state::AFTER_WRITE:
        frame->write_aw.await_resume();                  // co_await (B) завершился
        frame->promise.return_value(frame->doubled);     // co_return doubled;
        [[fallthrough]];
    case fsm_state::FINAL_SUSPEND:
        frame->state = fsm_state::DONE;                  // final_suspend = suspend_always -> done()
        return;
    case fsm_state::DONE:
        return;
    }
}

// ───────────────────────── драйверы ─────────────────────────
// Гоняем resume до завершения; считаем число resume и сколько раз исполнился
// прямой код между co_await (log_line).
static int run_real(int id, int& resume_count, std::size_t& log_count) {
    std::size_t log_before = g_log.size();
    Task<int> task = fetch_and_log(id);                  // ramp: создан, тело ещё не запущено
    resume_count = 0;
    while (!task.handle.done()) {                        // initial_suspend == suspend_always
        task.handle.resume();
        ++resume_count;
    }
    log_count = g_log.size() - log_before;
    return task.handle.promise().result;
}

static int run_manual(int id, int& resume_count, std::size_t& log_count) {
    std::size_t log_before = g_log.size();
    fetch_and_log_frame frame{};                         // ramp: построить frame...
    frame.id = id;                                       // ...скопировать параметр
    resume_count = 0;
    while (!frame.done()) {
        fetch_and_log_resume(&frame);
        ++resume_count;
    }
    log_count = g_log.size() - log_before;
    return frame.promise.result;
}

// ───────────────────────── тест ─────────────────────────
// Ручной state machine ведёт себя ровно как настоящая корутина: тот же
// результат, то же число resume, столько же раз исполнен код между co_await.
TEST(CoroutineLowering, ManualMatchesCompiler) {
    int real_steps = 0;
    int manual_steps = 0;
    std::size_t real_logs = 0;
    std::size_t manual_logs = 0;
    int real_result   = run_real(7, real_steps, real_logs);
    int manual_result = run_manual(7, manual_steps, manual_logs);

    EXPECT_EQ(real_result, (7 + 100) * 2);   // 214
    EXPECT_EQ(manual_result, real_result);   // ручной автомат — тот же результат
    EXPECT_EQ(manual_steps, real_steps);     // ...за то же число точек приостановки
    EXPECT_EQ(real_steps, 3);                // resume на каждый co_await + финальный
    EXPECT_EQ(manual_logs, real_logs);       // прямой код между co_await исполнён столько же раз
    EXPECT_EQ(real_logs, 1u);                // ...ровно один раз
}
