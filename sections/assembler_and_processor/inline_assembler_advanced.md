# Встроенный ассемблер в GCC (Inline Assembly)

GCC позволяет встраивать ассемблерные инструкции непосредственно в код на C/C++ с помощью оператора `asm`. Это позволяет использовать специфические инструкции процессора, недоступные через стандартный C, или написать точно оптимизированные участки кода без ущерба читаемости остальной программы.

## Синтаксис asm()

```
asm [volatile] (
    "инструкции"
    : выходные_операнды
    : входные_операнды
    : список_затрагиваемых_регистров
);
```

- **инструкции** — строка с ассемблерными командами в AT&T-синтаксисе; строки разделяются `\n\t`;
- **выходные операнды** — C-переменные, куда записывается результат;
- **входные операнды** — C-переменные или константы, используемые как входные данные;
- **затрагиваемые регистры (clobbers)** — список регистров, которые изменяет ассемблерная вставка.

Ссылки на операнды в строке инструкций: `%0` — первый операнд (первый выходной), `%1` — второй и т.д.

### Constraint-коды

| Код | Значение |
|-----|----------|
| `"r"` | любой регистр общего назначения |
| `"=r"` | выходной регистр (запись) |
| `"+r"` | регистр, который и читается, и записывается |
| `"m"` | ячейка памяти |
| `"i"` | непосредственная константа (immediate) |
| `"a"` | конкретно rax/eax |
| `"d"` | конкретно rdx/edx |
| `"g"` | регистр, память или константа (любой) |

В строке инструкций регистры пишутся с двойным процентом (`%%rax`).

## Базовые примеры

### Сложение двух переменных через ассемблер

```c
#include <stdio.h>

int main(void) {
    int a = 5, b = 3, result;

    asm("movl %1, %%eax\n\t"
        "addl %2, %%eax\n\t"
        "movl %%eax, %0"
        : "=r" (result)
        : "r"  (a), "r" (b)
        : "eax"
    );

    printf("result = %d\n", result);   /* result = 8 */
    return 0;
}
```

### Зачем нужен volatile

Без `volatile` компилятор вправе переместить, объединить или удалить ассемблерную вставку. С `volatile` вставка всегда выполняется ровно там, где написана:

```c
asm volatile("nop");         /* не будет удалена оптимизатором */
asm volatile("mfence");      /* барьер памяти — должен быть на месте */
```

### Обмен значений через xchg

```c
#include <stdio.h>

int main(void) {
    int a = 5, b = 10;
    printf("Before: a=%d, b=%d\n", a, b);

    asm("xchgl %0, %1"
        : "+r" (a), "+r" (b)
    );

    printf("After:  a=%d, b=%d\n", a, b);
    /* After: a=10, b=5 */
}
```

## Измерение тактов процессора через rdtsc

Инструкция `rdtsc` (Read Time-Stamp Counter) возвращает 64-битный счётчик тактов процессора. Это полезно для точного измерения производительности:

```c
#include <stdint.h>
#include <stdio.h>

uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

int main(void) {
    uint64_t start = rdtsc();
    for (int i = 0; i < 1000; i++)
        asm volatile("nop");
    uint64_t end = rdtsc();
    printf("Elapsed cycles: %lu\n", end - start);
}
```

Из-за out-of-order execution `rdtsc` может быть переставлена. Для точного измерения используют `cpuid` как барьер сериализации:

```c
uint64_t rdtsc_ordered(void) {
    uint32_t lo, hi;
    asm volatile(
        "xorl %%eax, %%eax\n\t"
        "cpuid\n\t"
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        :
        : "rbx", "rcx"
    );
    return ((uint64_t)hi << 32) | lo;
}
```

`cpuid` — непривилегированная инструкция; её главное свойство — полная сериализация: все предыдущие инструкции завершаются до `cpuid`, и никакие последующие не начинаются раньше неё. При `eax=0` она возвращает информацию о производителе процессора.

Альтернатива — инструкция `rdtscp`, которая сериализует только предшествующие нагрузки (load) и дополнительно возвращает идентификатор процессора в `ecx`:

```c
uint64_t rdtscp(uint32_t *cpu_id) {
    uint32_t lo, hi, aux;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    if (cpu_id) *cpu_id = aux;
    return ((uint64_t)hi << 32) | lo;
}
```

`rdtscp` является полным барьером для нагрузок: все предшествующие load-операции завершатся до выборки счётчика.

## Защита от timing-атак в криптографии

Timing-атака — атака по сторонним каналам, при которой противник измеряет время выполнения криптографической функции, чтобы восстановить секретный ключ или пароль.

### Уязвимая реализация:

```c
int insecure_compare(const char *input, const char *secret, int len) {
    for (int i = 0; i < len; i++) {
        if (input[i] != secret[i])
            return 0;   /* ранний выход — раскрывает позицию несовпадения! */
    }
    return 1;
}
```

### Constant-time реализация:

```c
#include <stdint.h>

int constant_time_compare(const uint8_t *a, const uint8_t *b, int len) {
    volatile uint8_t diff = 0;
    for (int i = 0; i < len; i++)
        diff |= a[i] ^ b[i];   /* XOR = 0 только при совпадении */
    return diff == 0;
}
```

`volatile` запрещает компилятору добавить ранний выход. Все `len` байт всегда проверяются, независимо от того, где обнаружено несовпадение.

В реальном производственном коде используйте готовые функции: `CRYPTO_memcmp` (OpenSSL), `sodium_memcmp` (libsodium), `timingsafe_bcmp` (glibc 2.33+).

## Связанные темы

- [Основы ассемблера](assembler_basics.md) — синтаксис AT&T, регистры
- [Ассемблерные функции и GDB](inline_assembler.md) — отдельные `.s`-файлы и отладка
- [Параллелизм на уровне инструкций](instruction_level_parallelism.md) — out-of-order execution и барьеры
- [Режимы процессора и системные вызовы](processor_modes_and_syscalls.md) — инструкция `syscall` через inline asm

## Источники

- GCC documentation: Extended Asm — https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html
- GCC documentation: Constraints — https://gcc.gnu.org/onlinedocs/gcc/Constraints.html
- Intel SDM Vol. 2: инструкции rdtsc, rdtscp, cpuid, xchg
- libsodium: constant-time comparison — https://libsodium.gitbook.io/doc/helpers
