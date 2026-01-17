    .globl add_numbers
    .type add_numbers, @function
add_numbers:
    # rdi = первое число, rsi = второе число
    movq %rdi, %rax     # rax = rdi
    addq %rsi, %rax     # rax += rsi
    ret                 # возврат (результат в rax)