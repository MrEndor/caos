##### Инструкции безусловного и условного перехода в ассемблере. Регистр флагов.

**jmp -- Безусловный прыжок:**

```asm
jmp label           ; прыгнуть на label
jmp *%rax          ; прыгнуть по адресу, хранящемуся в rax
```

**Регистр флагов (RFLAGS)** хранит статусные флаги:

| Флаг                 | Аббревиатура | Описание                                       |
|----------------------|--------------|------------------------------------------------|
| **Zero Flag**        | ZF           | =1, если результат последней операции = 0      |
| **Carry Flag**       | CF           | =1, если произошёл перенос/заём                |
| **Sign Flag**        | SF           | =1, если результат отрицательный (старший бит) |
| **Overflow Flag**    | OF           | =1, если переполнение при знаковых операциях   |
| **Parity Flag**      | PF           | =1, если чётное количество единиц в результате |
| **Auxiliary Carry**  | AF           | =1, если переполнение в полубайте              |
| **Direction**        | DF           | Направление для строковых операций             |
| **Interrupt Enable** | IF           | Разрешение на прерывания                       |

**Условные переходы (jcc -- jump if condition code):**

```asm
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
```

##### Какие есть условные переходы и как (идейно) они работают?

Условные переходы **проверяют значения флагов**, установленные предыдущей инструкцией (обычно `cmp` или арифметическая
операция):

```asm
cmpq %rbx, %rax         ; сравнить (вычислить rax - rbx, установить флаги)
je equal_label          ; если равны (ZF=1), прыгнуть
# ... код для неравных ...
equal_label:
# ... код для равных ...
```

Эквивалент в C:

```c
if (rax == rbx) {
    // равны
} else {
    // не равны
}
```

##### Как написать аналоги if, while и for на ассемблере?

**IF:**

```asm
# if (x > 0) {
#     print("positive");
# } else {
#     print("negative");
# }

movq x(%rip), %rax      # rax = x
testq %rax, %rax        # установить флаги по rax
jle negative            # если <= 0, прыгнуть на negative

# положительное число
# ... код ...
jmp end

negative:
# ... код ...

end:
```

**WHILE:**

```asm
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
```

**FOR:**

```asm
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
```

**Или с помощью loop инструкции:**

```asm
movq $10, %rcx          # rcx = счётчик
loop_start:
# ... тело цикла ...
loop loop_start         # rcx--, если rcx != 0, то прыгнуть на loop_start
```
