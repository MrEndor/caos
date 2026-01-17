# for (int i = 0; i < 10; i++) { ... }

movq $0, %rcx           # i = 0

for_loop:
cmpq $10, %rcx          # i ? 10
jge for_end             # если i >= 10, выход

# тело цикла
# ... код ...

incq %rcx               # i++
jmp for_loop

for_end:



movq $10, %rcx          # rcx = счётчик
loop_start:
# ... тело цикла ...
loop loop_start         # rcx--, если rcx != 0, то прыгнуть на loop_start