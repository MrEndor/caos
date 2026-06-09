// Вопрос 15: файберы (stackful coroutines), context switch, scheduler, yield, fault injection.
//
// Демонстрирует:
//   - Fiber:     собственный стек (malloc) + ucontext_t + entry-функция.
//   - Scheduler: ready-очередь Fiber*, current, main_ctx; spawn()/run()/yield().
//   - Кооперативную многозадачность: 3 фибры по очереди печатают свой id и делают yield.
//   - Fault injection: выбор следующей фибры ПСЕВДО-СЛУЧАЙНО (seeded RNG) вместо round-robin,
//     что позволяет детерминированно воспроизводить разные interleavings (как Loom/Shuttle/PCT).
//
// Идея context switch:
//   swapcontext сохраняет callee-saved регистры и текущий rsp (вершину стека) в один ucontext_t,
//   восстанавливает их из другого — фактически «подменяет стек». Стек у каждой фибры свой,
//   поэтому yield можно делать из ЛЮБОЙ глубины вложенности (stackful).
//   ucontext медленный, т.к. get/swapcontext по умолчанию ещё дёргают sigprocmask (syscall)
//   для сохранения/восстановления маски сигналов на каждом переключении.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q15_fibers/fiber_ucontext.cpp -o bin/q15_ucontext
// run:     ./bin/q15_ucontext

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <random>
#include <ucontext.h>
#include <vector>

// ───────────────────────── Fiber ─────────────────────────
struct Fiber {
    static constexpr std::size_t kStackSize = 64 * 1024;   // 64 KiB собственного стека

    ucontext_t            ctx{};
    std::vector<char>     stack;            // стек фибры (живёт, пока жива фибра)
    std::function<void()> fn;               // тело фибры
    bool                  finished = false;

    explicit Fiber(std::function<void()> body) : stack(kStackSize), fn(std::move(body)) {}
};

// ───────────────────────── Scheduler ─────────────────────────
class Scheduler {
public:
    enum class Mode { RoundRobin, FaultInjection };

    explicit Scheduler(Mode mode = Mode::RoundRobin, std::uint32_t seed = 0)
        : mode_(mode), rng_(seed) {}

    // Создать фибру и поставить её в ready-очередь.
    void spawn(std::function<void()> body) {
        auto* fib = new Fiber(std::move(body));
        getcontext(&fib->ctx);
        fib->ctx.uc_stack.ss_sp   = fib->stack.data();
        fib->ctx.uc_stack.ss_size = fib->stack.size();
        // Когда entry-функция фибры вернётся, управление уйдёт в main_ctx.
        fib->ctx.uc_link          = &main_ctx_;
        // makecontext принимает только функции (void(void)) с аргументами-int,
        // поэтому передаём указатель на фибру через два 32-битных слова.
        auto address = reinterpret_cast<std::uintptr_t>(fib);
        makecontext(&fib->ctx, reinterpret_cast<void (*)()>(&Scheduler::trampoline),
                    2, static_cast<unsigned>(address), static_cast<unsigned>(address >> 32));
        ready_.push_back(fib);
    }

    // Главный цикл: переключаемся в фибры, пока ready-очередь не опустеет.
    void run() {
        instance_ = this;
        while (!ready_.empty()) {
            current_ = pick_next();             // выбор зависит от режима
            // swapcontext: сохраняем контекст планировщика в main_ctx_,
            // прыгаем в выбранную фибру.
            swapcontext(&main_ctx_, &current_->ctx);
            // Вернулись сюда либо после yield(), либо после завершения фибры.
            if (current_->finished) {
                delete current_;
            } else {
                ready_.push_back(current_);     // не завершилась — обратно в очередь
            }
            current_ = nullptr;
        }
        instance_ = nullptr;
    }

    // Уступить процессор планировщику (вызывается из тела фибры).
    static void yield() {
        Scheduler* sched = instance_;
        Fiber* self      = sched->current_;
        // Сохраняем контекст фибры, возвращаемся в планировщик.
        swapcontext(&self->ctx, &sched->main_ctx_);
    }

private:
    // Выбор следующей фибры из ready-очереди.
    Fiber* pick_next() {
        if (mode_ == Mode::RoundRobin) {
            Fiber* front = ready_.front();
            ready_.pop_front();
            return front;
        }
        // Fault injection: берём случайный индекс из очереди.
        // Тот же seed -> тот же порядок (детерминированное воспроизведение interleaving'а).
        std::uniform_int_distribution<std::size_t> dist(0, ready_.size() - 1);
        std::size_t idx = dist(rng_);
        Fiber* picked = ready_[idx];
        ready_.erase(ready_.begin() + static_cast<std::ptrdiff_t>(idx));
        return picked;
    }

    // Точка входа фибры: собирает указатель обратно и вызывает её тело.
    static void trampoline(unsigned lo, unsigned hi) {
        auto address = (static_cast<std::uintptr_t>(hi) << 32) | static_cast<std::uintptr_t>(lo);
        Fiber* self  = reinterpret_cast<Fiber*>(address);
        self->fn();
        self->finished = true;
        // return -> сработает uc_link -> вернёмся в main_ctx_.
    }

    Mode                         mode_;
    std::mt19937                 rng_;
    std::deque<Fiber*>           ready_;
    ucontext_t                   main_ctx_{};
    Fiber*                       current_  = nullptr;
    static inline Scheduler*     instance_ = nullptr;   // для статического yield()
};

// ───────────────────────── демонстрации ─────────────────────────

static void run_demo(Scheduler::Mode mode, std::uint32_t seed, const char* title) {
    std::cout << "\n[" << title << "]\n";
    Scheduler sched(mode, seed);

    for (int id = 1; id <= 3; ++id) {
        sched.spawn([id] {
            for (int step = 0; step < 3; ++step) {
                std::cout << "  fiber " << id << " step " << step << "\n";
                Scheduler::yield();           // кооперативная уступка
            }
        });
    }
    sched.run();
}

int main() {
    std::cout << "=== Вопрос 15: файберы (ucontext) ===\n";

    // Round-robin: строгое чередование 1,2,3,1,2,3,...
    run_demo(Scheduler::Mode::RoundRobin, 0, "round-robin");

    // Fault injection: порядок зависит от seed -> воспроизводимые разные interleavings.
    run_demo(Scheduler::Mode::FaultInjection, 1, "fault-injection seed=1");
    run_demo(Scheduler::Mode::FaultInjection, 42, "fault-injection seed=42");

    std::cout << "\n=== готово ===\n";
    return 0;
}
