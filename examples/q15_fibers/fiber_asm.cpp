// Вопрос 15 (asm-вариант): файберы на СОБСТВЕННОМ ассемблерном context switch,
// без ucontext (см. context_switch.S, в духе Boost.Context jump_fcontext).
//
// Демонстрирует:
//   - Fiber:     собственный стек (std::vector<char>, выровнен по 16) + указатель
//                на сохранённую вершину стека (sp_) + entry std::function<void()>.
//   - Scheduler: ready-очередь Fiber*, current, контекст main; spawn()/run()/yield().
//   - Кооперативную многозадачность: 3 фибры по очереди печатают свой id и yield.
//
// Чем отличается от ucontext-версии (q15_ucontext):
//   * ucontext: swapcontext на каждом переключении ещё дёргает sigprocmask (syscall)
//     для сохранения/восстановления маски сигналов -> ~1 мкс на переключение.
//   * asm:      fiber_switch — это просто 6 push + смена rsp + 6 pop + ret, без
//     единого syscall -> ~десятки нс. Переключаем мы ровно callee-saved регистры
//     (rbx, rbp, r12-r15) и rsp; всё остальное — caller-saved, его и так «портит»
//     обычный вызов функции, так что трогать не нужно.
//
// Первый запуск новой фибры:
//   На дне её стека мы вручную раскладываем «фейковый» кадр так, будто фибра уже
//   когда-то вызывала fiber_switch и сейчас просто из него возвращается. Под 6
//   слотами callee-saved кладём адрес trampoline — туда «вернётся» ret внутри
//   fiber_switch при самом первом переключении на фибру.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q15_fibers/fiber_asm.cpp q15_fibers/context_switch.S -o bin/q15_asm
// run:     ./bin/q15_asm

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <vector>

// ───────────────────────── ассемблерный примитив ─────────────────────────
// Сохраняет callee-saved уходящего контекста на текущий стек, кладёт его rsp в
// *save_sp, грузит rsp из restore_sp, восстанавливает callee-saved, ret.
extern "C" void fiber_switch(void** save_sp, void* restore_sp);

// ───────────────────────── Fiber ─────────────────────────
struct Fiber {
    static constexpr std::size_t kStackSize = 64 * 1024;   // 64 KiB собственного стека

    std::vector<char>     stack;             // стек фибры (живёт, пока жива фибра)
    void*                 sp_      = nullptr;// сохранённая вершина стека этой фибры
    std::function<void()> body;              // тело фибры
    bool                  finished = false;

    explicit Fiber(std::function<void()> fn)
        : stack(kStackSize), body(std::move(fn)) {}

    // Разложить на дне стека фейковый кадр так, чтобы первый fiber_switch на эту
    // фибру «вернулся» в trampoline.
    void prepare(void (*trampoline)());
};

// Указатель на текущую исполняющуюся фибру — нужен trampoline'у, т.к. наш
// fiber_switch не передаёт аргументы в возобновляемый контекст (caller-saved
// регистры не сохраняются). Простой и проверенный паттерн.
static Fiber* g_current_fiber = nullptr;

void Fiber::prepare(void (*trampoline)()) {
    // Вершина стека = адрес сразу за буфером (стек растёт ВНИЗ).
    auto top = reinterpret_cast<std::uintptr_t>(stack.data()) + stack.size();
    // Выравниваем вершину по 16 байт.
    top &= ~static_cast<std::uintptr_t>(15);

    // Раскладываем слова сверху вниз.
    // SysV требует (rsp % 16 == 0) ПЕРЕД инструкцией call; значит при входе в
    // функцию (после того как call положил адрес возврата) rsp % 16 == 8.
    // ret внутри fiber_switch снимет адрес trampoline и прыгнет туда; на этот
    // момент rsp будет указывать на слот СРАЗУ ВЫШЕ адреса trampoline. Чтобы
    // там было rsp % 16 == 8, кладём один «фейковый» слот адреса возврата.
    auto* words = reinterpret_cast<std::uintptr_t*>(top);

    // [-1] фейковый адрес возврата из trampoline (trampoline сам делает switch,
    //      а не ret, но слот нужен для корректного выравнивания стека по SysV).
    words[-1] = 0;
    // [-2] адрес trampoline — сюда прыгнет ret внутри fiber_switch.
    words[-2] = reinterpret_cast<std::uintptr_t>(trampoline);
    // [-3..-8] шесть слотов под callee-saved (r15,r14,r13,r12,rbx,rbp) — их
    //          значения не важны, fiber_switch их просто popq в регистры.
    for (int i = 3; i <= 8; ++i) {
        words[-i] = 0;
    }

    // rsp новой фибры указывает на самый нижний из подготовленных слотов
    // (вершину «сохранённого» кадра) — туда fiber_switch начнёт свои popq.
    sp_ = &words[-8];
}

// ───────────────────────── Scheduler ─────────────────────────
class Scheduler {
public:
    // Создать фибру и поставить её в ready-очередь.
    void spawn(std::function<void()> fn) {
        auto* fib = new Fiber(std::move(fn));
        fib->prepare(&Scheduler::trampoline);
        ready_.push_back(fib);
    }

    // Главный цикл: переключаемся в фибры, пока ready-очередь не опустеет.
    void run() {
        instance_ = this;
        while (!ready_.empty()) {
            Fiber* fib = ready_.front();
            ready_.pop_front();
            current_        = fib;
            g_current_fiber = fib;
            // Сохраняем контекст планировщика в main_sp_, прыгаем в фибру.
            fiber_switch(&main_sp_, fib->sp_);
            // Вернулись сюда после yield() или завершения фибры.
            if (current_->finished) {
                delete current_;
            } else {
                ready_.push_back(current_);   // не завершилась — обратно в очередь
            }
            current_ = nullptr;
        }
        instance_ = nullptr;
    }

    // Уступить процессор планировщику (вызывается из тела фибры).
    static void yield() {
        Scheduler* sched = instance_;
        Fiber* self      = sched->current_;
        // Сохраняем контекст фибры в её sp_, возвращаемся в планировщик.
        fiber_switch(&self->sp_, sched->main_sp_);
    }

private:
    // Точка входа новой фибры. Вызывается ret'ом из fiber_switch при первом
    // переключении. Берёт текущую фибру из глобала, выполняет её тело, помечает
    // завершённой и переключается обратно в планировщик.
    static void trampoline() {
        Fiber* self = g_current_fiber;
        self->body();
        self->finished = true;
        // Возврат в планировщик навсегда: save_sp пишем в локальный мусор,
        // который нам больше не понадобится (фибра сюда уже не вернётся).
        void* discard = nullptr;
        fiber_switch(&discard, instance_->main_sp_);
    }

    std::deque<Fiber*>       ready_;
    void*                    main_sp_  = nullptr;   // сохранённая вершина стека планировщика
    Fiber*                   current_  = nullptr;
    static inline Scheduler* instance_ = nullptr;   // для статического yield()/trampoline()
};

// ───────────────────────── демонстрация ─────────────────────────

static void run_demo() {
    std::cout << "\n[round-robin (asm context switch)]\n";
    Scheduler sched;

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
    run_demo();
}
