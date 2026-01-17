movq %rax, %rbx         ; 64-бит: rbx = rax
movl $42, %eax          ; 32-бит: eax = 42 (обнулит верхние 32 бита rax)
movb $0xFF, %al         ; 8-бит: al = 0xFF
movq (%rax), %rbx       ; rbx = *(unsigned long *)rax (разыменование)
movq %rax, (%rbx)       ; *(unsigned long *)rbx = rax