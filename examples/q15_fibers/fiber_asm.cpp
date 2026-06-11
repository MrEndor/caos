// Вопрос 15 (asm-вариант): файберы на СОБСТВЕННОМ ассемблерном context switch,
// без ucontext (см. context_switch.S, в духе Boost.Context jump_fcontext).
//
// Демонстрирует:
//   - Fiber:     собственный стек (std::vector<char>, выровнен по 16) + указатель
//                на сохранённую вершину стека (sp) + entry std::function<void()>.
//   - Scheduler: ready-очередь фибр, текущая фибра, контекст main; spawn/run/yield.
//   - Кооперативную многозадачность: 3 фибры по очереди печатают свой id и yield.
//
// Чем отличается от ucontext-версии (q15_ucontext):
//   * ucontext: swapcontext на каждом переключении ещё дёргает sigprocmask (syscall)
//     для сохранения/восстановления маски сигналов -> ~1 мкс на переключение.
//   * asm:      fiber_switch — это 6 push + смена rsp + 6 pop + ret, без единого
//     syscall -> ~десятки нс. Переключаем ровно callee-saved регистры
//     (rbx, rbp, r12-r15) и rsp; всё остальное — caller-saved, его и так «портит»
//     обычный вызов функции, так что трогать не нужно.
//
// Первый запуск новой фибры:
//   На дне её стека вручную раскладываем «фейковый» кадр так, будто фибра уже
//   когда-то вызывала fiber_switch и сейчас просто из него возвращается. Под
//   шестью слотами callee-saved кладём адрес trampoline — туда «вернётся» ret
//   внутри fiber_switch при самом первом переключении на фибру.
//
// compile: g++ -std=c++20 -O2 -Wall -Wextra q15_fibers/fiber_asm.cpp q15_fibers/context_switch.S -o bin/q15_asm
// run:     ./bin/q15_asm

#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// ───────────────────────── ассемблерный примитив ─────────────────────────
// Сохраняет callee-saved уходящего контекста на текущий стек, кладёт его rsp в
// *save_sp, грузит rsp из restore_sp, восстанавливает callee-saved, ret.
extern "C" void fiber_switch(void** save_sp, void* restore_sp);

// Готовит дно стека новой фибры под формат восстановления в fiber_switch и
// возвращает её начальный sp. Реализация — context_switch.S.
extern "C" void* setup_context(void* stack_top, void (*entry)());

namespace {

// ───────────────────────── Fiber ─────────────────────────
struct Fiber {
    static constexpr std::size_t kStackSize = 64 * 1024;  // 64 KiB собственного стека

    std::vector<char>     stack;                  // стек фибры (жив, пока жива фибра)
    void*                 sp = nullptr;           // сохранённая вершина стека фибры
    std::function<void()> body;                   // тело фибры
    bool                  finished = false;

    explicit Fiber(std::function<void()> entry)
        : stack(kStackSize), body(std::move(entry)) {}
};

// ───────────────────────── Scheduler ─────────────────────────
// Кооперативный планировщик. Не реентерабельный: ровно один активный экземпляр
// (через instance_), потому что ассемблерный fiber_switch не умеет передавать
// `this` в возобновляемый контекст. yield()/trampoline() берут планировщик и
// текущую фибру из instance_.
class Scheduler {
public:
    // Создать фибру и поставить её в ready-очередь.
    void spawn(std::function<void()> entry) {
        auto fiber = std::make_unique<Fiber>(std::move(entry));
        // setup_context (asm) раскладывает фейковый кадр на дне стека и отдаёт sp,
        // с которым первый fiber_switch «вернётся» в trampoline.
        void* stack_top = fiber->stack.data() + fiber->stack.size();
        fiber->sp = setup_context(stack_top, &Scheduler::trampoline);
        ready_.push_back(std::move(fiber));
    }

    // Главный цикл: переключаемся в фибры, пока ready-очередь не опустеет.
    void run() {
        instance_ = this;
        while (!ready_.empty()) {
            current_ = std::move(ready_.front());
            ready_.pop_front();

            // Сохраняем контекст планировщика в main_sp_, прыгаем в фибру.
            fiber_switch(&main_sp_, current_->sp);

            // Вернулись сюда после yield() или завершения фибры.
            if (current_->finished) {
                current_.reset();                       // освобождаем фибру и её стек
            } else {
                ready_.push_back(std::move(current_));   // не завершилась — обратно
            }
        }
        instance_ = nullptr;
    }

    // Уступить процессор планировщику (вызывается из тела фибры).
    static void yield() {
        Fiber* self = instance_->current_.get();
        // Сохраняем контекст фибры в её sp, возвращаемся в планировщик.
        fiber_switch(&self->sp, instance_->main_sp_);
    }

private:
    // Точка входа новой фибры. Вызывается ret'ом из fiber_switch при первом
    // переключении: выполняет тело фибры, помечает её завершённой и уходит в
    // планировщик навсегда.
    static void trampoline() {
        Fiber* self = instance_->current_.get();
        // Исключение, выпущенное из тела фибры, раскручивалось бы наружу через
        // fiber_switch — голый asm без unwind-информации — и пробило бы дно стека
        // (UB). Поэтому гасим его здесь, на стеке самой фибры, не давая выйти за
        // trampoline. Фибра при этом штатно помечается finished.
        try {
            self->body();
        } catch (const std::exception& ex) {
            std::cerr << "  fiber exception: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "  fiber exception: (non-std)\n";
        }
        self->finished = true;

        // Возврат в планировщик навсегда. Контекст фибры сохранять некуда — фибра
        // сюда уже не вернётся, поэтому save_sp указывает на локальный «мусор».
        void* discard = nullptr;
        fiber_switch(&discard, instance_->main_sp_);
    }

    std::deque<std::unique_ptr<Fiber>> ready_;
    std::unique_ptr<Fiber>             current_;            // активная фибра (владеем)
    void*                              main_sp_ = nullptr;  // вершина стека планировщика
    static inline Scheduler*           instance_ = nullptr; // для статических yield/trampoline
};

// ───────────────────────── демонстрация ─────────────────────────
void run_demo() {
    std::cout << "\n[round-robin (asm context switch)]\n";
    Scheduler scheduler;

    for (int id = 1; id <= 3; ++id) {
        scheduler.spawn([id] {
            for (int step = 0; step < 3; ++step) {
                std::cout << "  fiber " << id << " step " << step << "\n";
                Scheduler::yield();                        // кооперативная уступка
            }
        });
    }

    // Фибра, бросающая исключение: trampoline ловит его, не давая раскрутке
    // пробить fiber_switch и дно стека. Планировщик продолжает работать.
    scheduler.spawn([] {
        std::cout << "  fiber 4 step 0\n";
        Scheduler::yield();
        throw std::runtime_error("boom from fiber 4");
    });

    scheduler.run();
}

}  // namespace

int main() {
    run_demo();
}
