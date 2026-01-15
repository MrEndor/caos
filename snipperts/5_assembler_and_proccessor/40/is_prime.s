.globl is_prime
.type is_prime, @function

is_prime:
    # rdi = число для проверки
    # возвращает 1 если простое, 0 иначе
    
    cmpq $2, %rdi        # число < 2?
    jl not_prime         # если да, не простое
    
    cmpq $2, %rdi        # число == 2?
    je is_prime_ret      # да, простое
    
    testq %rdi, %rdi     # чётное?
    movq $0, %rdx
    movq %rdi, %rax
    movq $2, %rcx
    divq %rcx
    cmpq $0, %rdx        # остаток == 0?
    je not_prime         # да, чётное, не простое
    
    movq $3, %rcx        # i = 3
    
loop:
    movq %rcx, %rax
    imulq %rcx, %rax     # i*i
    cmpq %rax, %rdi      # число < i*i?
    jl is_prime_ret      # да, простое
    
    movq %rdi, %rax
    movq $0, %rdx
    divq %rcx            # число % i
    cmpq $0, %rdx        # остаток == 0?
    je not_prime         # да, делится, не простое
    
    addq $2, %rcx        # i += 2
    jmp loop
    
is_prime_ret:
    movq $1, %rax        # return 1
    ret
    
not_prime:
    movq $0, %rax        # return 0
    ret
