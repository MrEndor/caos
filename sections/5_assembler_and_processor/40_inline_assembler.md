##### Как использовать в программе на Си ассемблерную функцию из другого файла?

Создаём отдельный файл с ассемблером (`.s` или `.asm`), компилируем его отдельно и линкуем с остальным кодом.

**Пример: функция проверки числа на простоту**

`is_prime.s` (AT&T синтаксис):

```asm
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
```

`main.c`:

```c
#include <stdio.h>

// объявить функцию из ассемблера
int is_prime(long n);

int main() {
    for (int i = 2; i <= 20; i++) {
        if (is_prime(i)) {
            printf("%d is prime\n", i);
        }
    }
    return 0;
}
```

**Сборка:**

```bash
gcc -c is_prime.s -o is_prime.o          # ассемблер -> объектный файл
gcc -c main.c -o main.o                  # C -> объектный файл
gcc is_prime.o main.o -o program         # линковка
./program
```

Или одной командой:

```bash
gcc main.c is_prime.s -o program
```

##### Как с помощью gdb делать отладку ассемблерного кода?

**Запуск gdb:**

```bash
gdb ./program
(gdb) run                           # запустить программу
```

**Просмотр ассемблерного кода:**

```gdb
(gdb) disassemble is_prime          # дизассемблировать функцию
(gdb) disassemble /m main           # показать с исходным кодом
(gdb) layout asm                    # режим TUI (Terminal User Interface) с ассемблером
```

**Точки останова (breakpoint) на ассемблерные инструкции:**

```gdb
(gdb) break *is_prime               # установить breakpoint на начало функции
(gdb) break *is_prime+10            # на инструкцию +10 байт от начала
(gdb) break is_prime.s:5            # на строку 5 в файле is_prime.s
```

**Просмотр регистров:**

```gdb
(gdb) info registers                # все регистры
(gdb) info registers rax rbx        # конкретные регистры
(gdb) p $rax                        # вывести значение rax
(gdb) p /x $rax                     # в шестнадцатеричном формате
(gdb) p /b $rax                     # в двоичном формате
```

**Пошаговое исполнение:**

```gdb
(gdb) stepi                         # одна ассемблерная инструкция (с заходом в функции)
(gdb) nexti                         # одна инструкция (без захода в функции)
(gdb) si                            # сокращённо stepi
(gdb) ni                            # сокращённо nexti
```

**Просмотр памяти:**

```gdb
(gdb) x/10x $rsp                    # показать 10 слов в формате hex с адреса rsp
(gdb) x/10gx $rsp                   # 10 quad-слов (64-бит)
(gdb) x/20i $rip                    # 20 инструкций с текущего адреса (rip)
```

**Просмотр стека:**

```gdb
(gdb) backtrace                     # стек вызовов
(gdb) frame 0                       # текущий фрейм
(gdb) info frame                    # информация о фрейме
```

**Пример сессии отладки:**

```gdb
$ gdb ./program
(gdb) break is_prime
Breakpoint 1 at 0x...
(gdb) run
Breakpoint 1, 0x... in is_prime ()
(gdb) disassemble
Dump of assembler code for function is_prime:
   0x... <+0>:	cmp    $0x2,%rdi
   0x... <+4>:	jl     0x... <is_prime+...>
   ...
=> 0x... <+...>:	// текущее положение (=>)
(gdb) info registers
rax            0x0                 0
rdi            0x7                 7          # число для проверки
...
(gdb) stepi
0x... in is_prime ()
(gdb) info registers rdi
rdi            0x7                 7
(gdb) continue
Breakpoint 1, ...
(gdb) quit
```

**Горячие клавиши в режиме TUI:**

- `Ctrl+X` + `A` -- toggle TUI режим;
- `Ctrl+X` + `2` -- показать исходный код и ассемблер;
- `Page Up/Down` -- скролировать окно.
