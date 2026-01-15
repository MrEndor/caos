##### Что такое атака переполнения буфера и какие средства защиты от нее существуют?

**Buffer Overflow (переполнение буфера)** -- атака, при которой программа записывает больше данных в буфер, чем он может
содержать, перезаписывая соседнюю память. Обычно используется для:

- перезаписи адреса возврата на стеке (hijack control flow);
- внедрения своего кода (shellcode) и его выполнения;
- отключения защит.

**Пример уязвимости:**

```c
void vulnerable(const char *input) {
    char buffer[64];
    strcpy(buffer, input);  // ОПАСНО! нет проверки длины
}
```

Если передать строку > 64 байт, произойдёт переполнение.

**Средства защиты:**

1. **Stack Protector (Canary)** -- проверка специального значения;
2. **ASLR** -- Address Space Layout Randomization;
3. **DEP/NX** -- Data Execution Prevention;
4. **RELRO** -- Relocation Read-Only;
5. **CFI** -- Control Flow Integrity;
6. **Safe функции** (strlcpy вместо strcpy);
7. **Compiler warnings** (-Wformat, -Wstrict-prototypes).

##### Что такое stack protector, для чего он используется, что делает флаг компиляции -fno-stack-protector?

**Stack Protector (Canary)** -- механизм обнаружения переполнения буфера на стеке:

1. Перед локальными переменными размещается **"canary"** (специальное случайное значение);
2. Перед возвратом из функции проверяется, не изменился ли canary;
3. Если изменился → произошло переполнение → программа завершается.

**Как это выглядит в ассемблере:**

```asm
func:
    push %rbp
    movq %rsp, %rbp
    subq $40, %rsp
    
    # Загрузить canary
    movq %fs:40, %rax       # rax = *(fs + 40) -- canary из TLS
    movq %rax, -8(%rbp)     # [rbp-8] = canary
    
    # ... код функции ...
    
    # Проверить canary перед возвратом
    movq -8(%rbp), %rax
    xorq %fs:40, %rax       # rax ^= canary из TLS
    je stack_ok             # если 0, canary OK
    
    call __stack_chk_fail   # иначе -- вызвать обработчик ошибки
stack_ok:
    leave
    ret
```

**Флаги:**

```bash
gcc -fstack-protector-all prog.c           # защита для всех функций (по умолчанию)
gcc -fno-stack-protector prog.c            # отключить canary
gcc -fstack-protector-strong prog.c        # защита для "опасных" функций
```

**Производительность:** минимальные накладные расходы (1-2%).

##### Как получить ошибку "Stack smashing detected"?

```c
#include <stdio.h>
#include <string.h>

void vulnerable(const char *input) {
    char buffer[8];
    strcpy(buffer, input);  // переполнение буфера
}

int main() {
    vulnerable("AAAAAAAAAAAAAAAA");  // строка > 8 байт
    return 0;
}
```

Компилируем и запускаем:

```bash
gcc -g prog.c -o prog       # с stack protector'ом (по умолчанию)
./prog
# Вывод: *** stack smashing detected ***: ./prog terminated
# Aborted (core dumped)
```

**Без stack protector'а:**

```bash
gcc -fno-stack-protector prog.c -o prog
./prog
# Segmentation fault (может быть, в зависимости от раскладки памяти)
```

##### Что такое ASLR и как его отключить?

**ASLR (Address Space Layout Randomization)** -- механизм ОС, который **случайно изменяет адреса** основных частей
процесса:

- стека;
- кучи;
- библиотек.

**Цель:** затруднить эксплуатацию, так как адреса непредсказуемы.

**Проверка статуса ASLR:**

```bash
cat /proc/sys/kernel/randomize_va_space
# 0 = отключено
# 1 = частичная рандомизация (только heap/stack)
# 2 = полная рандомизация (всё, включая библиотеки)
```

**Отключить ASLR (требует root):**

```bash
sudo sysctl -w kernel.randomize_va_space=0
```

**Отключить ASLR для одного процесса:**

```bash
setarch x86_64 -R ./prog    # запустить prog без ASLR
echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
```

**Включить обратно:**

```bash
sudo sysctl -w kernel.randomize_va_space=2
```

**В gdb ASLR отключается автоматически:**

```bash
gdb ./prog
(gdb) run                       # адреса будут стабильны
```

**Практическая демонстрация:**

```bash
./prog &
./prog
# С ASLR: разные адреса каждый раз
# Без ASLR: одинаковые адреса

# Проверить адрес:
gdb ./prog
(gdb) break main
(gdb) run
(gdb) info proc mappings       # показать адреса вс
