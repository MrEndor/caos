##### Как делать ассемблерные вставки в коде на Си?

**GCC Inline Assembly** (синтаксис AT&T):

```c
#include <stdio.h>

int main() {
    int a = 5, b = 3, result;
    
    // Базовая синтаксис
    asm("movl %1, %%eax\n\t"
        "addl %2, %%eax\n\t"
        "movl %%eax, %0"
        : "=r" (result)                 // output constraints
        : "r" (a), "r" (b)              // input constraints
        :                                // clobber list
    );
    
    printf("result = %d\n", result);    // 8
    return 0;
}
```

**Синтаксис asm():**

```
asm(code : outputs : inputs : clobbers);
```

- **code** -- строка с ассемблером;
- **outputs** -- переменные, куда пишется результат (`"=r"` = register);
- **inputs** -- переменные, откуда читается (`"r"` = register);
- **clobbers** -- регистры, которые процессор может испортить (например, `"rax"`).

**Constraint types:**

- `"r"` -- регистр (процессор выбирает);
- `"=r"` -- выходной регистр;
- `"+"` -- читаем и пишем;
- `"m"` -- память;
- `"i"` -- immediate (константа);
- `"g"` -- любой.

##### Зачем нужно слово volatile?

```c
volatile asm("nop");  // volatile asm не оптимизируется
```

**Без volatile:**

- компилятор может оптимизировать (удалить или переместить) ассемблерную вставку;
- если вставка имеет побочные эффекты (печать, модификация памяти), она может быть удалена.

**С volatile:**

- ассемблер всегда выполняется "как написано";
- нужен для I/O операций, timing-sensitive кода.

##### Приведите простейший пример, сделайте обмен значений двух переменных через ассемблерную вставку.

```c
#include <stdio.h>

int main() {
    int a = 5, b = 10;
    
    printf("Before: a=%d, b=%d\n", a, b);
    
    // Обмен через ассемблер (используя xchg)
    asm("xchgl %0, %1"
        : "+r" (a), "+r" (b)
    );
    
    printf("After: a=%d, b=%d\n", a, b);
    
    return 0;
}
```

**Вывод:**

```
Before: a=5, b=10
After: a=10, b=5
```

**Объяснение:**

- `"+r"` значит: регистр, в который пишем и из которого читаем;
- `xchgl a, b` обменивает значения;
- `%0` и `%1` -- первый и второй операнды (a и b).

##### Как с помощью ассемблерной вставки узнать количество тактов процессора, прошедшее между двумя данными строчками кода?

Используется инструкция `rdtsc` (read timestamp counter):

```c
#include <stdio.h>
#include <stdint.h>

uint64_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    uint64_t start = rdtsc();
    
    // код для измерения
    for (int i = 0; i < 1000; i++)
        asm("nop");  // холостая инструкция
    
    uint64_t end = rdtsc();
    
    printf("Elapsed cycles: %ld\n", end - start);
    
    return 0;
}
```

**Важно:**

- `"=a"` -- output в RAX;
- `"=d"` -- output в RDX;
- `volatile` -- не оптимизируется;
- `rdtsc` возвращает 64-битный timestamp.

**Чтобы избежать переупорядочивания инструкций (для точного измерения):**

```c
#include <stdio.h>
#include <stdint.h>

uint64_t rdtsc_ordered() {
    uint32_t lo, hi;
    // Сериализация: CPUID запрещает out-of-order execution
    asm volatile("xorl %%eax, %%eax\n\t"
                 "cpuid\n\t"
                 "rdtsc"
                 : "=a"(lo), "=d"(hi)
                 : 
                 : "rbx", "rcx");
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    uint64_t start = rdtsc_ordered();
    
    // код для измерения (он не будет переупорядочен)
    for (int i = 0; i < 1000000; i++)
        asm("nop");
    
    uint64_t end = rdtsc_ordered();
    
    printf("Elapsed cycles (ordered): %ld\n", end - start);
    
    return 0;
}
```

**CPUID:**

- Непривилегированная инструкция (в отличие от распространённого мнения);
- Требует EAX в качестве входного параметра (0 для базовой информации);
- Гарантирует сериализацию (все предыдущие инструкции завершены);
- Гарантирует, что последующие инструкции не начнутся раньше CPUID.

##### Как применяются ассемблерные вставки в криптографии для защиты от timing attacks?

**Timing Attack** -- атака, при которой измеряется время выполнения криптографического алгоритма, чтобы узнать секретный
ключ.

**Пример уязвивого кода:**

```c
int compare_passwords(const char *input, const char *stored) {
    for (int i = 0; i < strlen(stored); i++) {
        if (input[i] != stored[i])
            return 0;  // выход при первом несовпадении (ОПАСНО!)
    }
    return 1;
}
```

**Атака:** измерить, на каком символе происходит возврат, чтобы угадать пароль символ за символом.

**Защита через ассемблер (constant-time):**

```c
int compare_constant_time(const uint8_t *a, const uint8_t *b, int len) {
    volatile uint8_t result = 0;
    
    // Проверить ВСЕ байты, даже если нашли несовпадение
    for (int i = 0; i < len; i++) {
        result |= a[i] ^ b[i];  // XOR даёт 0 если совпадает
    }
    
    return result == 0;
}
```

**Дополнительная защита (ассемблер):**

```c
#include <stdint.h>

int compare_constant_time_asm(const uint8_t *a, const uint8_t *b, int len) {
    volatile uint8_t result = 0;
    
    // Запретить оптимизации и использовать volatile memory operands
    asm volatile(
        "xorl %%eax, %%eax\n\t"
        "1: cmpb (%1), (%2)\n\t"
        "   orl %%eax, %0\n\t"   // accumulate XOR result в memory
        "   incq %1\n\t"
        "   incq %2\n\t"
        "   decl %3\n\t"
        "   jnz 1b"
        : "+m"(result)
        : "r"(a), "r"(b), "r"(len)
        : "rax"
    );
    
    return result == 0;
}
```

**Почему это помогает:**

- все байты проверяются (нет раннего выхода);
- `volatile` предотвращает оптимизации;
- ассемблер гарантирует точный контроль над временем;
- timing attack'ер не может отличить "пароль неверен на 1-м символе" от "пароль неверен на последнем символе".

**В реальном коде:** использовать `memcmp_s` или `bcmp` из криптографических библиотек (OpenSSL, libsodium и т.д.).
