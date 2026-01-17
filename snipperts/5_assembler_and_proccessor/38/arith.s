addq %rbx, %rax         ; rax += rbx
subq $10, %rax          ; rax -= 10
imulq %rbx, %rax        ; rax *= rbx (signed)
idivq %rbx              ; rax = rdx:rax / rbx; rdx = остаток
incq %rax               ; rax++
decq %rax               ; rax--
negq %rax               ; rax = -rax