# while (x > 0) {
#     x--;
# }

loop_start:
movq x(%rip), %rax      # rax = x
cmpq $0, %rax           # x ? 0
jle loop_end            # если x <= 0, выход

decq %rax               # x--
movq %rax, x(%rip)      # сохранить x
jmp loop_start

loop_end: