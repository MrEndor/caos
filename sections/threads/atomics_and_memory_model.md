# Atomic operations и memory model

Lock-free код опирается не на mutex'ы, а на две вещи: **atomicity** (операция либо произошла целиком, либо не произошла
вовсе — без промежуточных состояний, видимых другим потокам) и **ordering** (правила, по которым записи одного потока
становятся видны другому). Без явного контроля над обоими аспектами многопоточная программа работает «как повезёт»: на
x86 кажется корректной, на ARM рассыпается.

Даже `x++` на одной разделяемой переменной — это **read-modify-write** (RMW): загрузка значения в регистр, инкремент,
запись обратно. На multi-core два потока могут одновременно прочитать одно и то же значение и записать одно и то же
+1 — итог потеряет одну инкрементацию. Атомарность инструкции `INC [mem]` на x86 без префикса `LOCK` не гарантируется.

C11 и C++11 ввели формальную модель памяти и `_Atomic`/`std::atomic`, которые компилятор отображает в правильные
машинные инструкции и memory barriers для каждой архитектуры. До этого приходилось писать inline assembly или
полагаться на платформенные интринсики (`__sync_*` в GCC).

## Atomicity на железе

x86-64 гарантирует атомарность для **aligned naturally-sized** загрузок и хранений:

| Размер  | Адрес выровнен по   | Атомарно без LOCK?     |
|---------|---------------------|------------------------|
| 1 байт  | любой               | да                     |
| 2 байта | 2                   | да                     |
| 4 байта | 4                   | да                     |
| 8 байт  | 8                   | да (на 64-bit CPU)     |
| 16 байт | 16, только `MOVDQA` | implementation-defined |

Невыровненный (`unaligned`) доступ, пересекающий cache line (64 байта), может разорваться на две транзакции к памяти.
Промежуточное состояние станет видно другому потоку — это **tearing**.

```
Aligned 8-byte load (атомарно)         Unaligned 8-byte load across cache lines
                                       (НЕ атомарно)
cache line N                           cache line N      cache line N+1
┌─────────────────────────────┐        ┌─────────────────┬─────────────────┐
│ . . . [ 8 bytes value ] . . │        │ . . . . [ 4 b ] │ [ 4 b ] . . . . │
└─────────────────────────────┘        └─────────────────┴─────────────────┘
        ▲                                       ▲                  ▲
        │ один MOV ─ одна транзакция            │ две транзакции — между ними
        │                                       │ другой поток может записать
```

### RMW и LOCK prefix

Инструкции read-modify-write (`INC`, `DEC`, `ADD`, `XADD`, `CMPXCHG`) **не** атомарны без `LOCK`:

```asm
inc dword [counter]      ; не атомарно: load, +1, store — три шага
lock inc dword [counter] ; атомарно: cache line заблокирована
```

Префикс `LOCK` блокирует cache line, содержащую операнд, на время выполнения инструкции. Раньше (Pentium и старше) это
была буквально блокировка системной шины — все остальные ядра останавливались. Современные процессоры используют
**cache coherency protocol** (MESI, см. [Кэши процессора](../assembler_and_processor/cpu_caches.md)): ядро переводит
cache line в состояние Modified и блокирует когерентность только для этой линии.

```
LOCK INC [addr] на двух ядрах одновременно

           Core 0                                    Core 1
           ┌─────────────────────────┐               ┌─────────────────────────┐
           │ LOCK INC [counter]      │               │ LOCK INC [counter]      │
           └────────────┬────────────┘               └────────────┬────────────┘
                        │                                         │
                        ▼                                         ▼
                ┌────────────────┐                       ┌────────────────┐
                │  L1 cache C0   │                       │  L1 cache C1   │
                │  [counter] = ? │                       │  [counter] = ? │
                └───────┬────────┘                       └───────┬────────┘
                        │                                         │
                        └───────────┬─────────────────────────────┘
                                    │
                                    ▼
                  ┌─────────────────────────────────────┐
                  │   Cache coherency (MESI)            │
                  │                                     │
                  │   1. C0 запрашивает M-состояние     │
                  │   2. C1 инвалидирует свою копию     │
                  │   3. C0 выполняет INC, выпускает    │
                  │   4. C1 запрашивает M-состояние     │
                  │   5. C0 инвалидирует свою копию     │
                  │   6. C1 выполняет INC               │
                  └─────────────────────────────────────┘

Результат: counter += 2 (две атомарные инкрементации сериализованы)
```

Стоимость:

| Операция                    | Латентность (порядок) |
|-----------------------------|-----------------------|
| Обычная инструкция в L1     | ~0.5 нс               |
| `LOCK`-операция uncontended | ~5–15 нс              |
| `LOCK` под contention       | 100+ нс на операцию   |

Инструкция `XCHG` с памятью имеет неявный `LOCK` — на x86 это исторически и есть самая дешёвая атомарная RMW для
реализации spinlock'а.

## Tearing на 32-битных платформах

На 32-битной x86 процессоре нет инструкции «прочитать 8 байт атомарно одной транзакцией». 64-битное чтение —
два 32-битных чтения подряд:

```
Поток A пишет 64-bit значение:                Поток B читает 64-bit значение:
  store low  = 0xCAFEBABE                       load  low  ──▶ 0xCAFEBABE
  store high = 0xDEADBEEF                       load  high ──▶ 0x00000000 (старое!)
                                                итог: 0x00000000CAFEBABE — мусор
```

Для корректного 64-битного atomic на 32-битах компилятор использует `CMPXCHG8B` или SSE-инструкции. Декларация
`_Atomic uint64_t` / `std::atomic<uint64_t>` это обеспечит, обычная переменная — нет.

Структуры `atomic<T>` выравнивают своё хранилище через `alignas(sizeof(T))`, чтобы исключить tearing. Свою атомарную
переменную всегда объявляйте через `_Atomic`/`std::atomic`, не полагайтесь на «у меня же оно выровнено».

## C11 и C++11 atomics API

| C11 (`<stdatomic.h>`)                       | C++11 (`<atomic>`)                    | Назначение                               |
|---------------------------------------------|---------------------------------------|------------------------------------------|
| `_Atomic int x;`                            | `std::atomic<int> x;`                 | объявление atomic переменной             |
| `atomic_load_explicit(&x, mo)`              | `x.load(mo)`                          | атомарное чтение                         |
| `atomic_store_explicit(&x, v, mo)`          | `x.store(v, mo)`                      | атомарная запись                         |
| `atomic_exchange_explicit(&x, v, mo)`       | `x.exchange(v, mo)`                   | записать, вернуть старое                 |
| `atomic_compare_exchange_strong(&x, &e, d)` | `x.compare_exchange_strong(e, d, mo)` | CAS, не допускает spurious fail          |
| `atomic_compare_exchange_weak(&x, &e, d)`   | `x.compare_exchange_weak(e, d, mo)`   | CAS, допускает spurious fail (LL/SC)     |
| `atomic_fetch_add_explicit(&x, v, mo)`      | `x.fetch_add(v, mo)`                  | атомарное `+=`, вернуть старое           |
| `atomic_fetch_sub/and/or/xor_explicit`      | `x.fetch_sub/and/or/xor(v, mo)`       | другие арифм./логические RMW             |
| `atomic_flag_test_and_set_explicit`         | `std::atomic_flag::test_and_set(mo)`  | atomic boolean для spinlock              |
| `atomic_thread_fence(mo)`                   | `std::atomic_thread_fence(mo)`        | standalone memory barrier                |
| `atomic_signal_fence(mo)`                   | `std::atomic_signal_fence(mo)`        | барьер только против compiler reordering |

Версии **без** суффикса `_explicit` (C) или **без** аргумента ordering (C++) используют `memory_order_seq_cst` — самый
сильный режим. Это безопасный дефолт, но самый дорогой.

`atomic<T>` для произвольного `T` компилируется в lock-based реализацию, если `T` слишком большой или его размер не
соответствует hardware-поддерживаемой атомарности. Проверка:

```cpp
std::atomic<MyStruct> a;
static_assert(std::atomic<MyStruct>::is_always_lock_free);  // compile-time
assert(a.is_lock_free());                                   // run-time
```

## Memory ordering

Источники переупорядочивания, против которых работает memory ordering:

1. **Компилятор** при оптимизации может переставить независимые операции для register allocation, dead store
   elimination, common subexpression elimination.
2. **CPU out-of-order execution** запускает инструкции не в program order, ориентируясь на готовность операндов.
3. **Cache memory subsystem** через store buffer и invalidation queue может задержать запись до того, как другие ядра
   её увидят.

C++ memory model описывает гарантии абстрактно — компилятор для каждой архитектуры подставит нужные fence-инструкции.

### memory_order_relaxed

Гарантирует только **atomicity**. Никаких ограничений на переупорядочение операций вокруг этой atomic-операции
относительно других операций (atomic или нет).

Применение: счётчики статистики, где важно не потерять инкременты, но порядок их «появления» не нужен.

```c
// относительно безвредно, можно relaxed
atomic_fetch_add_explicit(&request_count, 1, memory_order_relaxed);
```

### memory_order_acquire

Применяется к **load** (и acquire-части RMW). Никакая операция, **следующая** в program order за этой acquire-load, не
может быть переупорядочена **до** неё. Образует «нижнюю границу» барьера.

```
       любые операции              ◀── могут переехать сюда вниз
   ─────────────────────────
       acquire load            ◀── граница
   ─────────────────────────
       любые операции              ◀── НЕ могут переехать вверх через границу
```

### memory_order_release

Применяется к **store** (и release-части RMW). Никакая операция, **предшествующая** в program order этому
release-store, не может быть переупорядочена **после** него. «Верхняя граница» барьера.

```
       любые операции              ◀── НЕ могут переехать вниз через границу
   ─────────────────────────
       release store           ◀── граница
   ─────────────────────────
       любые операции              ◀── могут переехать сюда вверх
```

### memory_order_acq_rel

Только для RMW (например, `fetch_add`). Одновременно acquire по отношению к чтению и release по отношению к записи —
двусторонний барьер.

### memory_order_seq_cst

Acquire + release **плюс** единый глобальный total order для всех seq_cst операций во всей программе. Любой поток
видит seq_cst операции в одном и том же порядке. Это самая интуитивная модель — программа ведёт себя как чередование
инструкций потоков.

На x86 seq_cst для load — обычный `MOV`, для store — `MOV` + `MFENCE` (либо `XCHG`). На ARM/POWER seq_cst требует
полных барьеров с обеих сторон, поэтому заметно дороже acquire/release.

### memory_order_consume

Изначально задумывался как «более слабая версия acquire»: гарантировал ordering только для операций, **зависящих по
данным** от загруженного значения. На практике формальная спецификация оказалась неработоспособной — все компиляторы
сейчас апгрейдят `consume` до `acquire`. Стандарт C++17 пометил его «временно не рекомендуется».

### Таблица: что можно/нельзя переупорядочивать

```
┌──────────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│                  │   relaxed    │   acquire    │   release    │   seq_cst    │
├──────────────────┼──────────────┼──────────────┼──────────────┼──────────────┤
│ load → load      │   можно      │   нельзя*    │   можно      │   нельзя     │
│ load → store     │   можно      │   нельзя*    │   можно      │   нельзя     │
│ store → load     │   можно      │   можно      │   нельзя**   │   нельзя     │
│ store → store    │   можно      │   можно      │   нельзя**   │   нельзя     │
│ глобальный TO    │      нет     │      нет     │      нет     │      да      │
└──────────────────┴──────────────┴──────────────┴──────────────┴──────────────┘

*  для последующих (program order) операций относительно acquire-load
** для предшествующих (program order) операций относительно release-store
TO  = total order
```

## Канонический acquire/release pattern

Producer пишет данные, затем выставляет флаг готовности release-store'ом. Consumer читает флаг acquire-load'ом и, если
он установлен, читает данные.

```c
#include <stdatomic.h>
#include <threads.h>
#include <stdio.h>

int data;                       // обычная переменная
atomic_bool ready = false;

int producer(void *arg) {
    data = 42;                                                  // (1) обычная запись
    atomic_store_explicit(&ready, true, memory_order_release);  // (2) release
    return 0;
}

int consumer(void *arg) {
    while (!atomic_load_explicit(&ready, memory_order_acquire)) // (3) acquire
        ; /* spin */
    printf("data = %d\n", data);                                // (4) гарантированно 42
    return 0;
}
```

Гарантия acquire/release: если consumer на шаге (3) увидел `true`, то **все** записи, сделанные producer'ом до
release-store (включая `data = 42`), видны consumer'у. Это **happens-before** отношение.

```
Producer (thread A)                      Consumer (thread B)

  data = 42         (1)
        │
        │ sequenced-before
        ▼
  ready.store(true,             ─────────▶  ready.load(acquire) → true   (3)
              release)  (2)     synchronizes-with    │
                                                     │ sequenced-before
                                                     ▼
                                              read data → 42              (4)

       happens-before chain: (1) ─▶ (2) ─▶ (3) ─▶ (4)
```

Без acquire/release компилятор или CPU могут:

- переставить (1) после (2) → consumer увидит `ready=true` и `data=0`;
- переставить (4) перед (3) → consumer прочтёт `data` ещё до проверки флага.

## Compiler reordering и почему volatile не помогает

`volatile` придумали для **memory-mapped I/O**: чтобы компилятор не выкидывал «бесполезные» чтения регистра устройства
и не сливал записи. Это его единственное назначение.

Что `volatile` **делает**:

- Запрещает компилятору устранять или переставлять обращения **к одной и той же** volatile переменной.

Что `volatile` **не делает**:

- Не запрещает CPU переупорядочивать операции.
- Не делает RMW атомарным (`volatile int x; x++;` — три инструкции).
- Не упорядочивает доступ к **разным** переменным относительно друг друга.

```c
volatile int data;
volatile bool ready;

// producer
data = 42;
ready = true;
// (компилятор не переставит, но CPU на ARM/POWER — может!)

// consumer
if (ready)            // CPU может выполнить эту проверку позже чтения data
    use(data);        // ← может прочесть data до того, как реально установился флаг
```

В Java и C# `volatile` имеет другую семантику (acquire/release). В C/C++ — нет. Использование `volatile` для
многопоточности — один из самых распространённых багов в legacy-коде.

### x86 TSO vs слабые модели

| Архитектура | Модель                      | Что переупорядочивается без барьеров      |
|-------------|-----------------------------|-------------------------------------------|
| x86, x86-64 | **TSO** (Total Store Order) | только Store → Load (из-за store buffer)  |
| SPARC TSO   | TSO                         | то же                                     |
| ARMv7/v8    | weak                        | почти всё: L→L, L→S, S→L, S→S             |
| POWER       | weak                        | почти всё, плюс «cumulativity» странности |
| RISC-V      | weak (RVWMO)                | почти всё                                 |
| Alpha       | exceptionally weak          | даже dependent loads (легендарно)         |

Код, протестированный только на x86, может содержать неявные баги, которые проявятся на ARM. ThreadSanitizer
обнаруживает data races независимо от платформы — это его главная польза.

## Memory barriers на железе

Атомарные операции в C/C++ компилируются в нужные инструкции с подходящими барьерами. Standalone `atomic_thread_fence`
требуется редко: например, для синхронизации между потоком и signal handler (`atomic_signal_fence`), либо когда нужен
барьер без связанной атомарной операции.

| Архитектура | Полный барьер | Acquire fence | Release fence |
|-------------|---------------|---------------|---------------|
| x86-64      | `MFENCE`      | (no-op в TSO) | (no-op в TSO) |
| ARMv8       | `DMB ISH`     | `DMB ISHLD`   | `DMB ISH`     |
| POWER       | `sync`        | `lwsync`      | `lwsync`      |
| RISC-V      | `fence rw,rw` | `fence r,rw`  | `fence rw,w`  |

На x86 acquire/release fence — пустые инструкции, потому что TSO уже гарантирует их семантику аппаратно. Это и есть
причина, почему «плохо написанный» lock-free код часто проходит тесты на x86 — модель прощает многое.

`atomic_signal_fence(memory_order_seq_cst)` запрещает только компилятору переупорядочивать через эту точку — никаких
машинных инструкций не генерируется. Используется внутри signal handler'а, который выполняется в том же потоке.

## Compare-and-swap

`compare_exchange` атомарно делает: «если текущее значение равно `expected`, заменить на `desired` и вернуть `true`;
иначе записать в `expected` текущее значение и вернуть `false`».

```c
bool atomic_compare_exchange_strong_explicit(
    _Atomic(T) *obj,
    T *expected,
    T desired,
    memory_order success,    // ordering при успехе
    memory_order failure     // ordering при провале (не сильнее success)
);
```

На x86 компилируется в `LOCK CMPXCHG`. На ARM/POWER реализуется через LL/SC (load-linked / store-conditional) — пара
инструкций, где SC может «не сработать» по причинам, не связанным со значением (контекст переключился, cache line
украли). Поэтому есть два варианта:

- **`compare_exchange_strong`** — внутренне крутит цикл, никогда не возвращает false без причины.
- **`compare_exchange_weak`** — может «spuriously fail» даже при равенстве. Дешевле в LL/SC цикле, потому что
  внешний цикл всё равно нужен.

На x86 разницы нет — обе компилируются в одну инструкцию.

### CAS-loop для атомарного maximum

В стандартной библиотеке нет `fetch_max` (до C++26). Реализуем через CAS-loop:

```c
#include <stdatomic.h>

int atomic_fetch_max(_Atomic int *x, int newval) {
    int old = atomic_load_explicit(x, memory_order_relaxed);
    while (newval > old &&
           !atomic_compare_exchange_weak_explicit(
               x, &old, newval,
               memory_order_release,    // при успехе
               memory_order_relaxed)) { // при провале — просто перечитываем
        /* old обновлён внутри compare_exchange_weak */
    }
    return old;
}
```

Шаблон классический: загрузить, посчитать новое значение из старого, CAS, повторить при провале. Если CAS провалился,
`old` уже содержит свежее значение — не нужен новый `load`.

### ABA problem

CAS проверяет **равенство значений**, не идентичность. Между чтением и CAS значение могло измениться на B и
вернуться к A:

```
   Thread 1                        Thread 2
   ──────────                      ──────────
   read X → A
                                   X = B
                                   X = A
   CAS(X, A, A') → success
   ── но смысл данных уже другой! ──

Пример: lock-free stack
   head ──▶ A ──▶ B ──▶ C
   T1 хочет pop: читает head=A, видит A.next=B, готовится сделать CAS(head, A, B)

   T2 успевает: pop A, pop B, push A (другой контекст, но тот же адрес!)
   head ──▶ A ──▶ C

   T1 делает CAS(head, A, B) → success, но B уже не в списке!
   head ──▶ B (висячий указатель)
```

Лечения:

- **Tagged pointers**: вместе с указателем меняется счётчик ABA-tag, CAS делается через 128-битную CMPXCHG16B.
- **Hazard pointers** (Michael & Scott): поток объявляет, какой указатель он сейчас читает; никто не освобождает
  узел, пока он в hazard list.
- **RCU** (read-copy-update): отложенное освобождение до окончания «grace period».
- **Garbage collection**: в managed-языках ABA менее опасна, потому что узел не переиспользуется, пока есть ссылка.

## Полные примеры lock-free

### Relaxed-счётчик статистики

Если важен только итоговый счёт, а не порядок и не «happens-before» с другими данными:

```c
#include <stdatomic.h>
#include <threads.h>

atomic_uint requests_handled = 0;

void on_request(void) {
    atomic_fetch_add_explicit(&requests_handled, 1, memory_order_relaxed);
}

unsigned read_stats(void) {
    return atomic_load_explicit(&requests_handled, memory_order_relaxed);
}
```

На x86 это `LOCK XADD` — атомарно, но без MFENCE-обёртки, как было бы у seq_cst store. На ARM — экономит DMB.

### Spinlock через atomic_flag

`atomic_flag` — единственный тип в C11, который гарантированно lock-free на всех платформах.

```c
#include <stdatomic.h>

typedef struct {
    atomic_flag locked;
} spinlock_t;

#define SPINLOCK_INIT { ATOMIC_FLAG_INIT }

void spin_lock(spinlock_t *s) {
    while (atomic_flag_test_and_set_explicit(&s->locked, memory_order_acquire))
        ; /* busy-wait */
}

void spin_unlock(spinlock_t *s) {
    atomic_flag_clear_explicit(&s->locked, memory_order_release);
}
```

Acquire на захвате — чтобы записи в защищаемые данные не «утекли» в критическую секцию вперёд захвата. Release на
освобождении — чтобы записи внутри секции стали видны до того, как другой поток захватит lock.

В продакшне такой spinlock плох: под contention выжигает CPU. Минимальное улучшение — `pause` (x86) /`yield` (ARM) в
цикле ожидания. Серьёзный spinlock — ticket lock, MCS lock, либо futex-based mutex из glibc.

### SPSC очередь (single producer / single consumer)

Кольцевой буфер с двумя индексами. Один поток только пишет, другой только читает:

```c
#include <stdatomic.h>
#include <stdbool.h>

#define CAP 1024  // степень двойки

typedef struct {
    int buf[CAP];
    _Atomic size_t head;   // producer пишет сюда
    _Atomic size_t tail;   // consumer читает отсюда
} spsc_queue;

bool spsc_push(spsc_queue *q, int v) {
    size_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t == CAP) return false;     // full
    q->buf[h & (CAP - 1)] = v;
    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    return true;
}

bool spsc_pop(spsc_queue *q, int *out) {
    size_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    size_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (h == t) return false;           // empty
    *out = q->buf[t & (CAP - 1)];
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);
    return true;
}
```

```
SPSC queue (CAP = 8)

           tail                              head
            │                                 │
            ▼                                 ▼
         ┌────┬────┬────┬────┬────┬────┬────┬────┐
   buf:  │ -- │ -- │ A  │ B  │ C  │ -- │ -- │ -- │
         └────┴────┴────┴────┴────┴────┴────┴────┘
                     consumer ────▶  ◀──── producer

   consumer pop: читает buf[tail], tail++       ─ acquire load head ─ release store tail
   producer push: пишет buf[head], head++       ─ acquire load tail ─ release store head
```

Каждая сторона использует свой индекс relaxed (она единственный писатель), а противоположный — acquire (видеть
актуальное
положение оппонента и его записи в буфер). Release при обновлении своего индекса публикует запись в `buf`.

Тонкость: индексы `head`/`tail` лучше разместить на разных cache line через `alignas(64)`, иначе **false sharing**
сожжёт производительность (см. [Кэши процессора](../assembler_and_processor/cpu_caches.md)).

### Атомарный refcount (как в shared_ptr)

```cpp
#include <atomic>

struct control_block {
    std::atomic<int> ref{1};
    void *data;
};

void retain(control_block *cb) {
    cb->ref.fetch_add(1, std::memory_order_relaxed);
}

void release(control_block *cb) {
    if (cb->ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        // последний owner — освобождаем
        delete cb;
    }
}
```

Почему `relaxed` на `retain`? Увеличение счётчика не публикует и не наблюдает никакие данные — оно лишь говорит «есть
ещё один владелец». Никакой синхронизации с другими переменными не требуется.

Почему `acq_rel` на `release`? У `fetch_sub` две роли:

- **Release** часть: записи в объект, сделанные этим владельцем **до** освобождения, должны быть видны тому, кто
  выполнит финальный `delete`. Если их не публиковать, последний поток освободит объект, не «увидев» последних
  изменений.
- **Acquire** часть: тот, кто увидел refcount == 1 и идёт удалять, должен наблюдать все release-записи **всех
  предыдущих** владельцев.

Альтернатива из libc++/libstdc++ для производительности: `fetch_sub(1, release)` + `atomic_thread_fence(acquire)`
только в ветке `if (==1)`. Это исключает acquire-fence на каждом decrement.

## Стоимость на multi-core

```
Contended atomic counter — cache line bouncing

  4 потока, каждый дёргает atomic_fetch_add(&counter, 1)

           Core 0          Core 1          Core 2          Core 3
              │               │               │               │
              ▼               ▼               ▼               ▼
        ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
        │  L1 C0   │    │  L1 C1   │    │  L1 C2   │    │  L1 C3   │
        │ counter? │    │ counter? │    │ counter? │    │ counter? │
        └────┬─────┘    └────┬─────┘    └────┬─────┘    └────┬─────┘
             │               │               │               │
             └───────┬───────┴───────┬───────┴───────┬───────┘
                     │               │               │
                     ▼               ▼               ▼
              Cache line с counter «летает» между ядрами:
              Modified в C0 → Invalid в C1,C2,C3 → запрос от C1 →
              Modified в C1 → ... и так по кругу.

   Латентность одной инкрементации: 100–500 нс (cross-socket — ещё хуже)
   Throughput с одного потока  : ~100M ops/s
   Throughput с четырёх потоков: ~10M ops/s суммарно (хуже одного!)
```

Решения для high-contention счётчиков:

- **Per-CPU counters**: каждое ядро инкрементирует свой счётчик, чтение суммирует всё. В Linux kernel —
  `percpu_counter`. В user space — массив длиной `num_cpus`, индекс через `sched_getcpu()` или `getcpu()`.
- **Striped counters**: массив счётчиков, индекс по hash от thread id или адреса. Меньше точности «на лету», но не
  требует системного вызова. Часто хватает 16–64 stripe'ов.
- **Sharded statistics**: одна метрика на поток, агрегация при flush.

Главное правило: атомарность дёшева **per-instruction**, но дорога **per-cache-line** из-за coherency. Если можно
разнести по разным cache line — нужно это сделать.

## C++20 atomic_wait / atomic_notify

С C++20 у `std::atomic` появились методы `wait`, `notify_one`, `notify_all`:

```cpp
std::atomic<int> state{0};

// поток A
state.wait(0);              // блокируется, пока state == 0
do_work();

// поток B
state.store(1);
state.notify_one();         // будит одного waiter'а
```

Под капотом — `futex(FUTEX_WAIT/FUTEX_WAKE)` на Linux, `WaitOnAddress`/`WakeByAddressSingle` на Windows. Это полноценная
замена condition variable для простых случаев «жди, пока atomic примет нужное значение» — без отдельного `mutex` и
`condition_variable`. Подробнее про futex — в [Синхронизация потоков](thread_synchronization.md).

## Tooling для отладки

```bash
# ThreadSanitizer — runtime детектор data race, главный инструмент
gcc -fsanitize=thread -g -O1 prog.c -o prog
./prog
# Сообщает место конфликтующего доступа со stack trace обоих потоков.

# Helgrind — Valgrind tool, не требует пересборки, но медленнее
valgrind --tool=helgrind ./prog

# DRD — альтернативный Valgrind tool, чуть другие алгоритмы
valgrind --tool=drd ./prog
```

Для верификации lock-free алгоритмов:

- **cppmem** (онлайн): https://www.cl.cam.ac.uk/~pes20/cppmem/ — формальная модель C++ memory model, перебирает
  все допустимые исходы для короткого фрагмента.
- **herd7** из herdtools7: тестирует litmus-тесты против моделей x86 TSO, ARM, POWER, RISC-V.
- **Loom** (Rust), **Shuttle** (Rust/C++) — model checking, систематически проверяет все возможные interleaving'и.

ThreadSanitizer не находит баги, которые проявляются только при определённом memory ordering — он находит data race.
Корректность memory ordering проверяется только формальными методами или code review.

## Gotchas

- **`volatile` ≠ atomic.** Используйте `_Atomic`/`std::atomic`. `volatile` нужен только для MMIO и для setjmp/longjmp.
- **Default `seq_cst` — нормальный выбор.** Не оптимизируйте до acquire/release, не построив сначала рабочий
  seq_cst-вариант и не убедившись, что выигрыш значим.
- **SC-DRF теорема**: если программа data-race-free и использует только seq_cst, она ведёт себя как последовательная
  композиция шагов потоков (sequential consistency). Это та модель, которую интуитивно ожидают новички.
- **64-битные операции на 32-битной системе** без `_Atomic` могут разрываться. Не предполагайте атомарность по размеру.
- **`atomic<T>` для больших `T`** молча использует mutex. Проверяйте `is_always_lock_free` если важна lock-freedom.
- **Mixed-size атомарные операции** на одном адресе — UB. Не читайте 1 байт из 4-байтового atomic.
- **`compare_exchange_weak` без цикла** — баг. Spurious fail возможен на ARM/POWER даже без contention.
- **CAS-loop без `pause`/`yield`** под contention сжигает branch predictor и тратит энергию. В x86 — `_mm_pause()`.

## Связанные темы

- [Синхронизация потоков](thread_synchronization.md) — mutex, condvar, semaphore, futex под капотом
- [Кэши процессора](../assembler_and_processor/cpu_caches.md) — MESI, false sharing, стоимость contention
- [Параллелизм на уровне инструкций](../assembler_and_processor/instruction_level_parallelism.md) — почему CPU
  переупорядочивает
- [Inline-ассемблер](../assembler_and_processor/inline_assembler.md) — как написать atomic вручную через LOCK/CMPXCHG
- [Реализация потоков и clone](thread_implementation_clone.md) — как ядро создаёт потоки

## Источники

- Herb Sutter, «atomic<> Weapons» (видео, 2
  части) — https://herbsutter.com/2013/02/11/atomic-weapons-the-c-memory-model-and-modern-hardware/
- Preshing on Programming — https://preshing.com/ (особенно серия о memory ordering)
- C++ Memory Order, cppreference — https://en.cppreference.com/w/cpp/atomic/memory_order
- Anthony Williams, «C++ Concurrency in Action», 2nd ed., главы 5–7
- Paul McKenney, «Is Parallel Programming Hard, And, If So, What Can You Do About
  It?» — https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html
- Intel SDM, Vol. 3A, глава 8 «Multiple-Processor Management»
- ARM ARM, секция B2 «The AArch64 Application Level Memory Model»
- `man 7 futex`, исходники glibc `nptl/`
