je label            ; jump if equal (ZF=1)
jne label           ; jump if not equal (ZF=0)
jz label            ; jump if zero (ZF=1) -- аналог je
jnz label           ; jump if not zero (ZF=0) -- аналог jne

jl label            ; jump if less (для знаковых) -- SF != OF
jle label           ; jump if less or equal
jg label            ; jump if greater -- SF == OF && ZF == 0
jge label           ; jump if greater or equal

jb label            ; jump if below (для беззнаковых) -- CF=1
jbe label           ; jump if below or equal
ja label            ; jump if above -- CF=0 && ZF=0
jae label           ; jump if above or equal -- CF=0

jo label            ; jump if overflow (OF=1)
jno label           ; jump if no overflow (OF=0)
js label            ; jump if sign (SF=1)
jns label           ; jump if no sign (SF=0)



if (rax == rbx) {
    // равны
} else {
    // не равны
}

cmpq %rbx, %rax         ; сравнить (вычислить rax - rbx, установить флаги)
je equal_label          ; если равны (ZF=1), прыгнуть
# ... код для неравных ...
equal_label:
# ... код для равных ...