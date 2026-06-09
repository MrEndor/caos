/* Вопрос 15 (asm-вариант): ручной context switch без ucontext.
 *
 * x86-64 System V ABI. Тут только переключение регистрового контекста между
 * двумя стеками — никаких syscall'ов (в отличие от swapcontext, который ещё
 * дёргает sigprocmask для маски сигналов). Поэтому переключение ~десятки нс
 * против ~микросекунды у ucontext.
 *
 * Что сохраняем и почему:
 *   По SysV ABI callee-saved регистры (их обязана сберечь вызванная функция):
 *     rbx, rbp, r12, r13, r14, r15.
 *   Их мы кладём на ТЕКУЩИЙ стек, затем сохраняем rsp в *save_sp, переключаем
 *   rsp на restore_sp, снимаем оттуда callee-saved другой фибры и делаем ret.
 *
 *   Caller-saved регистры (rax, rcx, rdx, rsi, rdi, r8-r11) и FPU/SSE
 *   (xmm0-15 — caller-saved по SysV) НЕ сохраняем: вызывающий код уже считает
 *   их «затёртыми» обычным вызовом функции, а fiber_switch для него и есть
 *   обычный вызов функции. Поэтому их сохранение было бы лишней работой.
 *
 *   rip (адрес возврата) сохранять явно не нужно: его кладёт инструкция call,
 *   а восстанавливает ret — он лежит на стеке прямо под нашими callee-saved.
 */

    .text

/* void fiber_switch(void** save_sp, void* restore_sp)
 *   rdi = save_sp     — куда записать rsp уходящего контекста
 *   rsi = restore_sp  — откуда взять rsp приходящего контекста
 *
 * Раскладка кадра после push'ей (стек растёт вниз, [rsp] — вершина):
 *   [rsp+0]  = r15
 *   [rsp+8]  = r14
 *   [rsp+16] = r13
 *   [rsp+24] = r12
 *   [rsp+32] = rbx
 *   [rsp+40] = rbp
 *   [rsp+48] = адрес возврата (положил call)
 */
    .globl fiber_switch
    .type  fiber_switch, @function
fiber_switch:
    /* Сохраняем callee-saved уходящей фибры на её же стек. */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Запоминаем вершину стека уходящей фибры: *save_sp = rsp. */
    movq %rsp, (%rdi)

    /* Переключаем стек: rsp = restore_sp (вершина приходящей фибры). */
    movq %rsi, %rsp

    /* Снимаем callee-saved приходящей фибры в обратном порядке. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    /* ret снимет со стека приходящей фибры адрес возврата и прыгнет туда.
     * Для уже бежавшей фибры это точка после её fiber_switch.
     * Для НОВОЙ фибры это адрес trampoline, который мы подложили при
     * подготовке стека (см. Fiber::prepare в fiber_asm.cpp). */
    ret
    .size fiber_switch, .-fiber_switch

/* Помечаем стек как неисполняемый (без этого линкер ругается на .S). */
    .section .note.GNU-stack,"",@progbits
