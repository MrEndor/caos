# Ассемблерные функции и отладка в GDB

Одним из распространённых способов использовать ассемблер в проекте на языке C является написание отдельных функций в файлах `.s`, которые затем компилируются в объектные файлы и компонуются вместе с остальным кодом. Этот подход сохраняет читаемость исходников и позволяет точечно оптимизировать критические части программы.

## Ассемблерная функция из отдельного файла

Рассмотрим пример: функция проверки числа на простоту, реализованная на ассемблере и вызываемая из C.

`is_prime.s` (AT&T синтаксис):

```asm
    .globl is_prime
    .type  is_prime, @function

is_prime:
    # rdi = число для проверки; возвращает 1 если простое, 0 иначе
    cmpq $2, %rdi
    jl   not_prime          # число < 2: не простое

    cmpq $2, %rdi
    je   is_prime_ret       # число == 2: простое

    movq $0, %rdx
    movq %rdi, %rax
    movq $2, %rcx
    divq %rcx
    cmpq $0, %rdx
    je   not_prime          # чётное: не простое

    movq $3, %rcx           # i = 3

check_loop:
    movq %rcx, %rax
    imulq %rcx, %rax        # rax = i*i
    cmpq %rax, %rdi
    jl   is_prime_ret       # число < i*i: простое

    movq %rdi, %rax
    movq $0, %rdx
    divq %rcx               # rdx = число % i
    cmpq $0, %rdx
    je   not_prime          # нацело делится: не простое

    addq $2, %rcx           # i += 2
    jmp  check_loop

is_prime_ret:
    movq $1, %rax
    ret

not_prime:
    movq $0, %rax
    ret
```

`main.c`:

```c
#include <stdio.h>

int is_prime(long n);   /* объявление функции из ассемблера */

int main(void) {
    for (int i = 2; i <= 20; i++) {
        if (is_prime(i))
            printf("%d is prime\n", i);
    }
    return 0;
}
```

Сборка:

```bash
gcc -c is_prime.s -o is_prime.o   # ассемблер -> объектный файл
gcc -c main.c     -o main.o       # C -> объектный файл
gcc is_prime.o main.o -o program  # компоновка
./program
```

Можно также передать оба файла компилятору сразу:

```bash
gcc main.c is_prime.s -o program
```

GCC распознаёт расширение `.s` и вызывает ассемблер автоматически.

## Отладка ассемблерного кода с помощью GDB

GDB (GNU Debugger) поддерживает пошаговое выполнение на уровне отдельных машинных инструкций, просмотр регистров и памяти.

### Запуск и точки останова

```bash
gcc -g is_prime.s main.c -o program   # собрать с отладочной информацией
gdb ./program
```

```gdb
(gdb) run                    # запустить программу
(gdb) break is_prime         # точка останова на входе в функцию
(gdb) break *is_prime+10     # точка останова на смещении +10 байт
(gdb) break is_prime.s:5     # точка останова на строке 5 файла .s
```

### Просмотр ассемблерного кода

```gdb
(gdb) disassemble is_prime        # дизассемблировать функцию
(gdb) disassemble /m main         # совместить исходный код и ассемблер
(gdb) layout asm                  # TUI-режим с окном ассемблера
(gdb) layout src                  # TUI-режим с окном исходного кода
```

Текущая инструкция помечается стрелкой `=>` в выводе `disassemble`.

### Пошаговое выполнение

```gdb
(gdb) stepi   # (si) — выполнить одну машинную инструкцию, заходя в функции
(gdb) nexti   # (ni) — выполнить одну инструкцию, не заходя в функции
(gdb) continue               # продолжить до следующей точки останова
```

### Просмотр регистров и памяти

```gdb
(gdb) info registers          # все регистры общего назначения
(gdb) info registers rax rdi  # конкретные регистры
(gdb) p $rax                  # значение rax в десятичном виде
(gdb) p /x $rax               # в шестнадцатеричном
(gdb) p /t $rax               # в двоичном
(gdb) x/10gx $rsp             # 10 quad-слов (64-бит) начиная с rsp
(gdb) x/20i $rip              # 20 инструкций начиная с текущего rip
(gdb) x/s $rdi                # строка по адресу в rdi
```

### Стек вызовов

```gdb
(gdb) backtrace               # цепочка стековых фреймов
(gdb) frame 0                 # выбрать фрейм 0 (текущий)
(gdb) info frame              # подробности о выбранном фрейме
```

### Пример сессии

```gdb
$ gdb ./program
(gdb) break is_prime
Breakpoint 1 at 0x401130
(gdb) run
Breakpoint 1, 0x0000000000401130 in is_prime ()
(gdb) disassemble
Dump of assembler code for function is_prime:
=> 0x401130 <+0>:  cmp    $0x2,%rdi
   0x401134 <+4>:  jl     0x401160 <is_prime+48>
   ...
(gdb) info registers rdi
rdi   0x7   7
(gdb) stepi
(gdb) info registers rflags
rflags  0x202  [ IF ]
(gdb) continue
(gdb) quit
```

### Горячие клавиши TUI-режима

- `Ctrl+X`, `A` — включить/выключить TUI;
- `Ctrl+X`, `2` — показать одновременно окна исходного кода и ассемблера;
- `Page Up` / `Page Down` — прокрутка активного окна.

## Связанные темы

- [Основы ассемблера](assembler_basics.md) — синтаксис AT&T, регистры, базовые инструкции
- [Стековые фреймы и вызовы функций](stack_frames_and_function_calls.md) — как работают `call`/`ret`, ABI
- [Встроенный ассемблер в GCC](inline_assembler_advanced.md) — `asm()` для встраивания ассемблера прямо в C-код

## Источники

- `man 1 gdb` — руководство по GNU Debugger
- `man 1 as` — GNU Assembler
- GDB documentation: https://sourceware.org/gdb/documentation/
- Beej's Guide to GDB: https://beej.us/guide/bggdb/
