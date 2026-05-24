# Предсказание ветвлений (Branch Prediction)

Современные процессоры работают по принципу конвейера (pipeline): пока одна инструкция выполняется, следующие уже
находятся на стадиях выборки и декодирования. Это позволяет завершать несколько инструкций за один такт. Однако условные
переходы нарушают конвейер: до вычисления условия неизвестно, какую инструкцию выбирать следующей.

## Идея предсказания ветвлений

Без предсказания процессор вынужден остановить конвейер и ждать результата сравнения, прежде чем продолжить выборку
инструкций. На суперскалярных процессорах с глубиной конвейера 15–20 стадий это означает потерю 15–20 тактов при каждом
непредсказанном переходе.

Решение — **branch predictor**: специальный аппаратный блок, который анализирует историю переходов и делает ставку на
наиболее вероятный исход ещё до вычисления условия. Если предсказание верно, конвейер продолжает работу без простоя.
Если нет — происходит сброс (flush) неверно выбранных инструкций (mispredict penalty: 15–20 тактов).

Современные процессоры предсказывают верно ~97% переходов, что позволяет почти всегда избегать штрафа.

```
Pipeline без предсказания (branch stall):

Такт:   1        2        3        4        5        6        7        8
        ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
Fetch   │  cmp   │  jge   │ stall  │ stall  │ stall  │ instr A│ instr B│  ...
        ├────────┼────────┼────────┼────────┼────────┼────────┼────────┤
Decode  │        │  cmp   │  jge   │ stall  │ stall  │ stall  │ instr A│
        ├────────┼────────┼────────┼────────┼────────┼────────┼────────┤
Execute │        │        │  cmp   │  jge   │◀ результат известен      │
        └────────┴────────┴────────┴────────┴────────┴────────┴────────┘
                                    ← 3 такта простоя (bubble) →

Pipeline с branch prediction (верное предсказание):

Такт:   1        2        3        4        5        6        7        8
        ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
Fetch   │  cmp   │  jge   │ instr A│ instr B│ instr C│ instr D│ instr E│  ← спекулятивно
        ├────────┼────────┼────────┼────────┼────────┼────────┼────────┤
Decode  │        │  cmp   │  jge   │ instr A│ instr B│ instr C│ instr D│
        ├────────┼────────┼────────┼────────┼────────┼────────┼────────┤
Execute │        │        │  cmp   │  jge ✓ │ instr A│ instr B│ instr C│
        └────────┴────────┴────────┴────────┴────────┴────────┴────────┘
                                        ↑ предсказание верно — простоев нет

Pipeline с branch prediction (неверное предсказание — mispredict):

Такт:   1        2        3        4        5        6        7        8
        ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
Fetch   │  cmp   │  jge   │ instr A│ instr B│ flush  │ flush  │ instr X│  ← правильный путь
        ├────────┼────────┼────────┼────────┼────────┼────────┼────────┤
Decode  │        │  cmp   │  jge   │ instr A│ flush  │ flush  │ flush  │
        ├────────┼────────┼────────┼────────┼────────┼────────┼────────┤
Execute │        │        │  cmp   │  jge ✗ │← flush спекулятивного    │
        └────────┴────────┴────────┴────────┴────────┴────────┴────────┘
                                    ← mispredict penalty: 15–20 тактов →
```

## Эксперимент: влияние branch prediction на производительность

Классический эксперимент сравнивает суммирование элементов, превышающих порог, для отсортированного и неотсортированного
массивов:

```cpp
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

int main() {
    constexpr int SIZE = 32768;
    std::vector<int> data(SIZE);

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int& val : data)
        val = dist(rng);

    using Clock = std::chrono::steady_clock;

    /* Тест 1: неотсортированные данные (плохое предсказание) */
    auto start = Clock::now();
    long sum = 0;
    for (int i = 0; i < SIZE; ++i)
        if (data[i] >= 128)
            sum += data[i];
    auto end = Clock::now();
    auto time_unsorted = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "Unsorted:  " << time_unsorted << " ns  (sum=" << sum << ")\n";

    std::sort(data.begin(), data.end());

    /* Тест 2: отсортированные данные (хорошее предсказание) */
    start = Clock::now();
    sum = 0;
    for (int i = 0; i < SIZE; ++i)
        if (data[i] >= 128)
            sum += data[i];
    end = Clock::now();
    auto time_sorted = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    std::cout << "Sorted:    " << time_sorted << " ns  (sum=" << sum << ")\n";
    std::cout << "Speedup:   " << static_cast<double>(time_unsorted) / time_sorted << "x\n";
}
```

Ожидаемый результат:

```
Unsorted:  1200 ns  (sum=...)
Sorted:     400 ns  (sum=...)
Speedup:   3.0x
```

При неотсортированных данных условие `data[i] >= 128` выполняется приблизительно в случайном порядке — предсказатель
угадывает лишь ~50% переходов, что даёт много штрафов. При отсортированном массиве сначала идут все элементы < 128 (
переход всегда не выполняется), затем все >= 128 (всегда выполняется) — предсказатель справляется почти идеально.

## Подсказки компилятору о вероятности ветвей

Программист может явно указать компилятору, какая ветка является основной, чтобы компилятор разместил её код оптимальнее
с точки зрения выборки инструкций.

**C++20 атрибуты:**

```cpp
if (data[i] >= 128) [[likely]] {
    sum += data[i];
}

if (error_occurred) [[unlikely]] {
    handle_error();
}
```

**GCC/Clang для C и старых стандартов:**

```c
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

if (likely(data[i] >= 128))
    sum += data[i];

if (unlikely(ptr == NULL))
    return -1;
```

`__builtin_expect(expr, expected)` сообщает компилятору, что значение `expr` скорее всего равно `expected`.

## Влияние подсказок на генерируемый ассемблер

Без подсказки компилятор размещает «выпадающий» код (false-branch) после условного перехода. С `[[likely]]` компилятор
организует код так, что вероятная ветка следует линейно, а маловероятная убирается в другое место:

```asm
# С [[likely]] для ветки sum += data[i]:
    cmpq $128, (%rdi, %rcx, 4)
    jl   .unlikely_branch        # перейти на маловероятную ветку
    addq (%rdi, %rcx, 4), %rax   # вероятная ветка — идёт линейно
    jmp  .continue
.unlikely_branch:
    # ... обработка редкого случая ...
.continue:
```

Линейное расположение вероятной ветки улучшает работу i-cache и аппаратного prefetcher'а.

## Устранение ветвлений с помощью cmov

В некоторых случаях лучший способ справиться с плохо предсказываемым переходом — полностью его устранить, заменив на
условное перемещение (`cmov`):

```c
/* С ветвлением — плохо предсказывается на случайных данных */
if (a > b) result = a; else result = b;

/* Без ветвления — всегда одинаково быстро */
result = (a > b) ? a : b;  /* GCC часто компилирует в cmovg */
```

```asm
    cmpq %rsi, %rdi
    movq %rsi, %rax
    cmovg %rdi, %rax    /* rax = rdi, только если rdi > rsi */
```

`cmov` не меняет состояние конвейера независимо от условия и поэтому никогда не вызывает mispredict.

## Спекулятивное выполнение и атаки класса Spectre

Branch predictor — основа спекулятивного выполнения: процессор начинает выполнять инструкции по предсказанному пути ещё
до того, как условие проверено. Если предсказание оказалось неверным, архитектурное состояние (регистры, память)
откатывается — но **микроархитектурное** состояние (кэш, TLB, BTB) откатить не успевают. На этом разрыве построен целый
класс уязвимостей.

### Spectre v1 — Bounds Check Bypass (CVE-2017-5753)

Идея атаки: натренировать предсказатель ветвлений так, чтобы он спекулятивно выполнил доступ к массиву с индексом **за
границей**, а затем по cache-timing восстановить прочитанный байт.

Уязвимый код в ядре/гипервизоре/JIT:

```c
uint8_t array1[16];
uint8_t array2[256 * 64];   /* probe array, 64 = cache line size */

uint8_t victim(size_t x) {
    if (x < sizeof(array1)) {        /* (1) bounds check */
        return array2[array1[x] * 64];  /* (2) speculative load */
    }
    return 0;
}
```

Атака:

1. Многократно вызывают `victim(x)` с валидными `x` (0..15) — predictor запоминает, что bounds check почти всегда true.
2. Сбрасывают `array2` из кэша (`clflush`).
3. Вызывают `victim(x)` с `x = адрес_секрета - адрес_array1`. CPU спекулятивно делает `array1[x]` → читает секретный
   байт `s`, затем `array2[s * 64]` — подтягивает в кэш конкретную линию.
4. Bounds check заканчивается → CPU понимает, что спекуляция была неверной, откатывает архитектурное состояние. Но
   кэш-линия в `array2` осталась загруженной.
5. Атакующий измеряет время доступа к каждой из 256 кэш-линий `array2`. Самая быстрая = `s`.

```
Такты CPU при спекулятивном чтении секрета:

T+0   ┌─ if (x < 16)             ← predictor говорит "true" (натренирован)
      │
T+1   ├─ load array1[x]          ← x за границей, читает секретный байт s
      │  (спекулятивно, в register reorder buffer)
      │
T+5   ├─ compute s * 64
      │
T+6   ├─ load array2[s*64]       ← подтягивает cache line, индекс зависит от s
      │  (спекулятивно)
      │                          ┌─────────────────────────────────┐
      │                          │  L1: cache line[s*64] загружена │
      │                          └─────────────────────────────────┘
T+20  ├─ x < 16 = FALSE          ← bounds check резолвится
      │  rollback архитектурного
      │  состояния (регистры)
      │  ❌ кэш НЕ откатывается
      │
      ▼
Атакующий: for (i=0; i<256; i++) measure_time(array2[i*64]);
           самое быстрое i = значение секретного байта s
```

Скорость утечки — до ~2 KB/s. На больших окнах спекуляции (CPU с глубоким ROB — Reorder Buffer) удаётся вытащить ядерную
память из user space.

### Spectre v2 — Branch Target Injection (CVE-2017-5715)

Цель — заставить процессор спекулятивно перейти **на чужой код** через косвенный вызов (`call *%rax`, `jmp *%rax`,
виртуальная функция C++). Адрес такого перехода предсказывает **Branch Target Buffer (BTB)** — таблица «по адресу
инструкции `call` → предсказанная цель».

BTB **разделяется между процессами** (на CPU без раздельных тэгов): атакующий из своего процесса многократно делает
`call *%rax` по тому же виртуальному адресу, что и жертва, направляя его на адрес **gadget'а** в коде жертвы (
последовательность вроде Spectre v1). При следующем вызове жертвой косвенного call'а CPU предсказывает по BTB и
спекулятивно начинает выполнять gadget жертвы — с её привилегиями.

```
Atacker process               Victim process (kernel, hypervisor)

train:                        execute:
  call *%rax  ──┐               call *%rcx  ──┐
   target =    │                                │
   gadget_addr │                                │
              ▼                                ▼
        ┌──────────────────────┐    ┌─────────────────────────────┐
        │   Branch Target      │    │ predictor говорит:          │
        │   Buffer (BTB)       │◀───┤ "цель = gadget_addr"        │
        │ [PC → target]        │    │ (запись от атакующего)      │
        └──────────────────────┘    └─────────────────────────────┘
                                                │
                                                ▼
                                    спекулятивно выполняется gadget
                                    с привилегиями victim →
                                    утечка через cache timing
```

### Mitigations

#### lfence — барьер спекуляции для Spectre v1

`lfence` (Load Fence) — инструкция, которая запрещает спекулятивное выполнение последующих load'ов до того, как все
предыдущие инструкции ретайрятся. Вставляется **между** bounds check и зависимым load'ом:

```c
if (x < sizeof(array1)) {
    asm volatile("lfence" ::: "memory");   /* барьер */
    return array2[array1[x] * 64];
}
```

GCC/Clang генерируют это автоматически при `-mspeculative-load-hardening`. В Linux есть макрос
`array_index_nospec(index, size)` — реализован через маску, а не через `lfence` (быстрее):

```c
/* include/linux/nospec.h, упрощённо */
#define array_index_nospec(idx, sz) ({          \
    typeof(idx) _i = (idx);                     \
    typeof(sz)  _s = (sz);                      \
    typeof(_i)  _mask = array_index_mask_nospec(_i, _s);  \
    _i & _mask;                                 \
})
```

При `_i >= _s` маска становится 0 — индекс зануляется даже в спекулятивном пути.

#### retpoline — защита от Spectre v2

`retpoline` (return + trampoline) — программная замена косвенного перехода `jmp *%rax`. Идея: использовать `ret`, чьё
предсказание идёт через **Return Stack Buffer (RSB)**, а не через BTB, и наполнить RSB безопасным значением.

```asm
# Было:
    jmp *%rax                # косвенный прыжок, BTB-предсказание = уязвимо

# Стало (retpoline):
    call set_up_target       # call → push retaddr на стек
capture_speculation:
    pause                    # спекулятивный CPU крутится здесь
    lfence
    jmp capture_speculation  # бесконечный цикл (не достигнет ret)
set_up_target:
    mov %rax, (%rsp)         # переписываем return address на target
    ret                      # ret использует RSB, не BTB → безопасно
```

Что происходит по тактам:

```
1. call set_up_target          ─┐
   ├─ push return = capture_speculation
   ├─ RSB.push(capture_speculation)   ← RSB знает: следующий ret вернётся сюда
   └─ jmp set_up_target

2. mov %rax, (%rsp)             ─┐  Архитектурно: переписываем return address
                                 │  на настоящий target. Но это видно только
                                 │  после retire mov'а.

3. ret                            ─┐  Спекулятивно: предсказание идёт из RSB
   ├─ predict: capture_speculation │  → CPU начинает выполнять pause/lfence/jmp
   │  → попадает в бесконечный     │  → не утечёт никаких данных
   │     цикл (безопасный sink)    │
   └─ architecturally: jump to     ← когда mov ретайрится, ret видит настоящий
      настоящий *%rax                адрес и перенаправляется туда
```

`pause` снижает энергопотребление и нагрузку на pipeline в спекулятивном цикле. `lfence` гарантирует, что спекуляция не
уйдёт дальше.

Цена: косвенный вызов с 1 такта вырастает до ~20 тактов. На горячем коде с виртуальными функциями (C++ vtable, indirect
call в interpreter loop) замедление 5–10%. GCC включает retpoline через
`-mindirect-branch=thunk -mfunction-return=thunk`.

#### IBRS / IBPB / STIBP — микрокодовые барьеры

Аппаратные mitigations через MSR (Model-Specific Registers), требуют обновления микрокода:

| Барьер                                               | Назначение                                                                                     | Стоимость                                  |
|------------------------------------------------------|------------------------------------------------------------------------------------------------|--------------------------------------------|
| **IBRS** (Indirect Branch Restricted Speculation)    | Запрещает предсказателю в high-privilege режиме использовать записи, сделанные в low-privilege | Высокая, ~20–50% на syscall-heavy workload |
| **IBPB** (Indirect Branch Predictor Barrier)         | Сбрасывает BTB при context switch — следующий процесс не видит обучения предыдущего            | Умеренная, разовая при switch              |
| **STIBP** (Single Thread Indirect Branch Predictors) | На SMT (Hyper-Threading) запрещает sibling-потоку влиять на предсказания                       | Умеренная, постоянная при включенном SMT   |

Текущее состояние mitigations в Linux:

```bash
# Что включено для каждой уязвимости
grep . /sys/devices/system/cpu/vulnerabilities/*

# Пример вывода:
# .../spectre_v1:Mitigation: usercopy/swapgs barriers and __user pointer sanitization
# .../spectre_v2:Mitigation: Retpolines, IBPB: conditional, IBRS_FW, STIBP: conditional
# .../meltdown:Mitigation: PTI
```

### Meltdown (CVE-2017-5754) — отдельный класс

Meltdown эксплуатирует не предсказатель, а out-of-order execution: CPU спекулятивно выполняет загрузку из kernel-памяти
**до** проверки привилегий (на затронутых Intel-процессорах эта проверка отложена). За окно спекуляции значение попадает
в cache, дальше — тот же cache-timing exfiltration, что в Spectre v1.

Mitigation — **KPTI** (Kernel Page Table Isolation): kernel-страницы убираются из page tables пользовательского режима,
в user space видны только страницы trampoline'ов для входа в ядро. Подробнее —
в [memory_protection.md](../memory/memory_protection.md).

## Связанные темы

- [Условные переходы и флаги](conditional_jumps_flags.md) — инструкции `jcc`, `cmov`, `setcc`
- [Параллелизм на уровне инструкций](instruction_level_parallelism.md) — конвейер, out-of-order execution, уязвимость
  Spectre
- [Кэши процессора](cpu_caches.md) — взаимодействие i-cache с предсказателем ветвлений, MESI и cache-timing атаки
- [Защита памяти](../memory/memory_protection.md) — KPTI как mitigation против Meltdown

## Источники

- Intel 64 and IA-32 Architectures Optimization Reference Manual — Chapter 3: Branch Prediction
- Agner Fog, "Optimizing software in C++", Chapter 15: https://agner.org/optimize/
- GCC documentation: `__builtin_expect` — https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
- Kocher et al., "Spectre Attacks: Exploiting Speculative Execution": https://spectreattack.com/spectre.pdf
- Lipp et al., "Meltdown: Reading Kernel Memory from User Space": https://meltdownattack.com/meltdown.pdf
- Intel, "Retpoline: A Branch Target Injection
  Mitigation": https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/technical-documentation/retpoline-branch-target-injection-mitigation.html
- Linux kernel docs: `Documentation/admin-guide/hw-vuln/spectre.rst`
