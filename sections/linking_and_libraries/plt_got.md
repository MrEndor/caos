# PLT/GOT и lazy binding

В динамически слинкованной программе каждый вызов внешней функции из `.so` идёт не напрямую, а через
два уровня переадресации: **PLT** (Procedure Linkage Table) — таблица крошечных trampoline-заглушек, и
**GOT** (Global Offset Table) — массив указателей, который заполняет динамический линковщик. Этот трюк
существует по двум независимым причинам.

Первая — **позиционно-независимый код** (PIC). Чтобы одна `.so` могла загружаться по разным адресам у
разных процессов и при этом её страницы `.text` оставались общими в RAM, в `.text` нельзя зашивать
абсолютные адреса. Все обращения к глобальным данным и внешним функциям проходят через GOT, а GOT —
это уже writable-секция, индивидуальная для каждого процесса.

Вторая — **lazy binding**. Программа с сотней зависимостей вызывает из libc, может быть, тридцать
функций. Резолвить адреса всех сотен тысяч экспортируемых символов при старте — потеря времени. Lazy
binding откладывает поиск до первого вызова: пока функция не вызвана, её GOT-запись указывает обратно
в PLT, и обращение к ней запускает резолвер.

## Layout: .plt, .plt.got, .got, .got.plt

Современный glibc-бинарь содержит четыре связанных секции:

```
 .got       — обычная Global Offset Table:
              указатели на ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ из .so
              (например, &errno, &environ)

 .got.plt   — отдельная GOT именно для PLT-функций
              (FUNC-релокации, заполняется лениво)

 .plt       — таблица trampoline'ов для FUNC-вызовов
              работает в паре с .got.plt

 .plt.got   — альтернативные trampoline'ы для функций,
              у которых уже взяли адрес (например, через
              указатель на функцию); резолвятся eager
```

Зачем разделение `.got` и `.got.plt`? Объекты в `.got.plt` нужно перезаписывать в runtime (туда пишет
резолвер), а объекты в `.got` после старта программы изменяться не должны. Это позволяет включить
защиту памяти RELRO для `.got`, оставив `.got.plt` writable (см. ниже).

Расположение в реальном бинаре:

```bash
readelf -S ./prog | grep -E '\.(plt|got)'
#   .plt              PROGBITS  ...  AX  16
#   .plt.got          PROGBITS  ...  AX   8
#   .got              PROGBITS  ...  WA   8
#   .got.plt          PROGBITS  ...  WA   8
```

Флаги: `AX` — Allocate + eXecute (код), `WA` — Write + Allocate (данные). PLT — код, GOT — данные.

## Структура PLT-записи

Каждая PLT-запись на x86-64 — 16 байт. Первая запись (`PLT[0]`) особенная — это «общий трамплин» к
резолверу:

```
 .plt          (читается дизассемблером)

 PLT[0]:                                  ◀── общий трамплин, вызывается
   push  qword [.got.plt + 8]                 первой PLT-инструкцией каждой
   jmp   qword [.got.plt + 16]                функции после прыжка в неё
   nop ...                                    (.got.plt+8 — link_map объекта,
                                               .got.plt+16 — _dl_runtime_resolve)

 PLT[1] (printf@plt):
   jmp   qword [.got.plt + 24]            ◀── при первом вызове ведёт обратно
   push  0                                    в эту же запись (на push 0);
   jmp   PLT[0]                                после резолва ведёт прямо в printf

 PLT[2] (puts@plt):
   jmp   qword [.got.plt + 32]
   push  1
   jmp   PLT[0]
```

`push N` кладёт на стек **индекс релокации** — он же индекс символа в `.rela.plt`. Резолвер по этому
индексу узнает, какую именно функцию надо найти.

Соответствующие записи в `.got.plt`:

```
 .got.plt                                          назначение

 [0]  адрес секции .dynamic                        служебное
 [1]  link_map текущего объекта                    кладётся ld.so при старте
 [2]  адрес _dl_runtime_resolve                    кладётся ld.so при старте
 [3]  адрес "PLT[1]+6" (push 0; jmp PLT[0])        изначально ←── ЛОВУШКА
 [4]  адрес "PLT[2]+6" (push 1; jmp PLT[0])        изначально ←── ЛОВУШКА
 ...
```

Хитрость в том, что `.got.plt[3]` (для printf) **изначально** содержит указатель назад в `PLT[1]+6` —
сразу после первой инструкции `jmp [got.plt+24]`. Поэтому первый вызов уходит туда, попадает на
`push 0; jmp PLT[0]`, и через `PLT[0]` оказывается в резолвере. После резолва туда же будет записан
настоящий адрес `printf` в libc, и второй вызов уже прыгнет напрямую.

## Lazy binding шагами

```
 1-Й ВЫЗОВ printf("hello")

   main:
     call printf@plt    ──┐
                          │
     ┌────────────────────┘
     ▼
   PLT[1] (printf@plt):
     jmp qword [.got.plt + 24]    ──┐
                                    │ .got.plt[3] = адрес PLT[1]+6 (ловушка)
     ┌──────────────────────────────┘
     ▼
   PLT[1]+6:
     push 0                          ◀── индекс printf в .rela.plt
     jmp  PLT[0]                  ──┐
                                    │
     ┌──────────────────────────────┘
     ▼
   PLT[0]:
     push qword [.got.plt + 8]      ◀── link_map
     jmp  qword [.got.plt + 16]   ──┐  _dl_runtime_resolve
                                    │
     ┌──────────────────────────────┘
     ▼
   _dl_runtime_resolve (в ld.so):
     • argv-аналог: link_map + reloc_index
     • _dl_lookup_symbol — BFS по scope (см. dynamic_linker.md)
     • находит printf по адресу 0x7ffff7e3b400 в libc.so
     • ЗАПИСЫВАЕТ этот адрес в .got.plt[3]   ◀── ВАЖНО
     • jmp 0x7ffff7e3b400 (реальный printf)
                                    │
                                    ▼
                              printf выполняется,
                              возвращается прямо в main

 2-Й ВЫЗОВ printf

   main:
     call printf@plt    ──┐
                          │
     ▼
   PLT[1]:
     jmp qword [.got.plt + 24]    ──┐ теперь .got.plt[3] = 0x7ffff7e3b400
                                    ▼
                              printf — напрямую, без резолвера
```

Стоимость первого вызова — порядка сотен наносекунд (`_dl_lookup_symbol` плюс кэш-промахи). Стоимость
второго и далее — одна лишняя инструкция `jmp` через GOT, то есть несколько тактов.

Посмотреть весь процесс на конкретном бинаре:

```bash
gdb ./prog
(gdb) start
(gdb) disassemble printf@plt
(gdb) b *printf@plt
(gdb) run
(gdb) x/gx 0x404020      # адрес .got.plt[printf] до резолва
(gdb) finish             # выйти из первого вызова printf
(gdb) x/gx 0x404020      # теперь там реальный адрес функции в libc
```

## BIND_NOW / LD_BIND_NOW: eager binding

Lazy binding отключается тремя способами, на трёх разных уровнях:

| Способ                       | Действие                                                |
|------------------------------|---------------------------------------------------------|
| `LD_BIND_NOW=1 ./prog`       | env-флаг для ld.so, разовый запуск                      |
| `-Wl,-z,now`                 | флаг линковки: в `.dynamic` выставляется `DF_BIND_NOW`  |
| `LD_BIND_NOT=1`              | анти-вариант: НЕ обновлять GOT после резолва (отладка)  |

Когда BIND_NOW активен, `ld.so` при старте резолвит **все** функции и заполняет всю `.got.plt` сразу.
PLT по-прежнему используется (`call printf@plt` всё так же есть в `.text`), но `_dl_runtime_resolve`
больше никогда не вызывается.

Зачем это нужно:

- **Защита от race conditions** — ленивый резолв не thread-safe в редких сценариях (две нити вызывают
  одну и ту же функцию первый раз одновременно). Современный glibc делает резолв атомарно, но
  исторически были баги.
- **Предсказуемость latency** — в системах реального времени никто не хочет, чтобы первый вызов
  функции «случайно» занял в 100 раз больше времени, чем последующие.
- **Защита от GOT-overwrite** (вместе с RELRO, см. ниже) — если все указатели GOT уже расставлены при
  старте, то можно сделать `.got.plt` read-only.
- **Раннее обнаружение** missing-символов — без BIND_NOW программа упадёт только когда вызовет
  конкретную несуществующую функцию.

```bash
# Сравнить время старта программы с многими зависимостями
time LD_BIND_NOW=0 ./prog --version
time LD_BIND_NOW=1 ./prog --version
```

Для крупных приложений (например, KDE-аппликаций или Chromium) разница может быть десятки миллисекунд.
Поэтому современные дистрибутивы часто собирают бинари с lazy binding, а для security-критичного кода
(sshd, sudo) — с `-z,now`.

## RELRO и Full RELRO

RELRO (RELocation Read-Only) — техника защиты GOT и других служебных таблиц от перезаписи через
переполнение буфера.

```
 БЕЗ RELRO (-Wl,-z,norelro)

 ┌─────────────┬─────────────┬─────────────┬──────────────┐
 │   .text     │   .data     │   .got      │   .got.plt   │
 │   r-x       │   rw-       │   rw-       │   rw-        │   ← всё writable
 └─────────────┴─────────────┴─────────────┴──────────────┘
                                ▲              ▲
                                │              │
              Атакующий, имея запись по произвольному адресу,
              может перезаписать .got[errno] или .got.plt[free]
              на свой shellcode

 PARTIAL RELRO (-Wl,-z,relro)  — по умолчанию у gcc

 ┌─────────────┬─────────────┬─────────────┬──────────────┐
 │   .text     │   .data     │   .got      │   .got.plt   │
 │   r-x       │   rw-       │   r--       │   rw-        │   ← .got защищён
 └─────────────┴─────────────┴─────────────┴──────────────┘
                                ▲              ▲
                                │              │
                       больше не writable     ОСТАЁТСЯ writable
                       (после старта)         (для lazy binding)

 FULL RELRO (-Wl,-z,relro,-z,now)

 ┌─────────────┬─────────────┬─────────────┬──────────────┐
 │   .text     │   .data     │   .got      │   .got.plt   │
 │   r-x       │   rw-       │   r--       │   r--        │   ← обе read-only
 └─────────────┴─────────────┴─────────────┴──────────────┘
                                              ▲
                                              │
                               защищено, но требует eager binding
                               (медленнее старт)
```

Механика Partial RELRO: линковщик кладёт `.got` рядом с другими «one-time-init» данными в специальный
сегмент `PT_GNU_RELRO`. После того как ld.so закончил все начальные релокации, он делает `mprotect`
этого сегмента в `PROT_READ`. Дальнейшая запись в `.got` приводит к `SIGSEGV`.

Full RELRO просто включает в `PT_GNU_RELRO` ещё и `.got.plt` — но это возможно только если резолвер
больше не будет туда писать, то есть только при `-z,now`.

```bash
# Проверить, какой RELRO у бинаря
readelf -l ./prog | grep RELRO

checksec --file=./prog
#   Full RELRO         | Partial RELRO   | No RELRO
```

## GOT-overwrite атаки

Классическая техника эксплуатации, актуальная в начале 2000-х и до сих пор работающая против бинарей
без Full RELRO. Сценарий:

1. В программе есть уязвимость, позволяющая записать произвольное значение по произвольному адресу
   (write-what-where), — например, через format string vulnerability в `printf(user_input)`.
2. Атакующий узнаёт адрес `.got.plt[free]` (по `objdump -R prog` это видно даже без отладочной
   информации).
3. Записывает по этому адресу указатель на нужный код — например, на `system()` из libc.
4. Когда программа в следующий раз вызывает `free(ptr)`, на самом деле вызывается `system(ptr)`.

```
 Программа собрана без RELRO, .got.plt[free] = 0x7f...real_free

   *(uintptr_t*)0x404038 = (uintptr_t)system;   // через format string

 Дальше любой call free@plt:

   call free@plt
       │
       ▼
   jmp [.got.plt + N]    ── теперь там адрес system
       │
       ▼
   system(ptr) — выполнен пользовательский ввод

 С Full RELRO:

   *(uintptr_t*)0x404038 = ...                  // SIGSEGV в момент записи
```

Полная защита достигается комбинацией Full RELRO + ASLR + Stack Canaries + NX. Каждый отдельный
механизм можно обойти; вместе они делают цепочки эксплуатации существенно сложнее.

## ASLR и PLT/GOT

ASLR (Address Space Layout Randomization) рандомизирует базовые адреса сегментов: `.text`, `.data`,
heap, stack, и **каждой загружаемой `.so`** ld.so присваивает случайный смещённый адрес.

PLT/GOT эта рандомизация не ломает, потому что:

- PLT-записи внутри одного бинаря смещены друг относительно друга на фиксированные значения. Когда
  бинарь PIE и загружается со случайной базой, PLT смещается вся целиком.
- GOT-записи заполняются в runtime реальными адресами функций — а где конкретно загружена libc.so,
  ld.so уже знает на момент резолва.

```
 Запуск №1:
   PIE base = 0x55b3a0000000
     .text      0x55b3a0001100
     .plt       0x55b3a0001040
     .got.plt   0x55b3a0003fa0
   libc base = 0x7f8c12000000
     printf     0x7f8c1203b400

 Запуск №2 (другой процесс, та же программа):
   PIE base = 0x563f80000000      ◀── другие адреса
     .text      0x563f80001100
     .plt       0x563f80001040
     .got.plt   0x563f80003fa0
   libc base = 0x7fa3e8000000
     printf     0x7fa3e803b400    ◀── но смещения внутри libc те же
```

Поэтому для атакующего знание относительных смещений внутри libc (через утечку любого её адреса)
сразу даёт адреса всех её функций — это техника **info leak + ret2libc**.

## .plt.got: для взятых адресов функций

Если код берёт адрес функции (`void (*p)(void) = printf;`), а потом вызывает по указателю — lazy
binding не подходит, потому что сам адрес «утекает» наружу, и резолвер некому будет вызвать. Для таких
случаев линковщик создаёт отдельную секцию `.plt.got`:

```
 .plt.got               простой trampoline без push/jmp PLT[0]:

   printf.got:
     jmp qword [.got + N]   ◀── .got[N] заполнен сразу при старте,
     nop                          а не лениво

 .got                   обычная GOT, охраняется Partial RELRO

   [N]  адрес printf в libc.so   ◀── записано ld.so при старте
```

Поэтому если ваш бинарь активно использует function pointers на библиотечные функции, многие из них
резолвятся eager даже без `-z,now`.

## Связанные темы

- [Динамический линковщик (ld.so) изнутри](dynamic_linker.md) — как именно `_dl_runtime_resolve` находит символ
- [Формат ELF](elf_format.md) — секции `.plt`, `.got`, `.dynamic`, релокации
- [Статическая и динамическая линковка](static_dynamic_linking.md) — `LD_BIND_NOW`, общая картина lazy binding
- [Защита от переполнения буфера](../assembler_and_processor/buffer_overflow_protection.md) — RELRO в общем контексте mitigations
- [Защита памяти](../memory/memory_protection.md) — `mprotect`, NX, ASLR

## Источники

- [System V AMD64 ABI: Procedure Linkage Table](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- Ulrich Drepper, [How To Write Shared Libraries](https://www.akkadia.org/drepper/dsohowto.pdf) — главы про PLT/GOT
- [glibc: sysdeps/x86_64/dl-trampoline.S, elf/dl-runtime.c](https://sourceware.org/git/?p=glibc.git) — реализация `_dl_runtime_resolve`
- [Hardening ELF binaries using Relocation Read-Only — Redhat](https://www.redhat.com/en/blog/hardening-elf-binaries-using-relocation-read-only-relro)
- [GOT and PLT for pwning — systemoverlord.com](https://systemoverlord.com/2017/03/19/got-and-plt-for-pwning.html)
- `man ld` — флаги `-z relro`, `-z now`, `-z nodelete`
- John R. Levine, [Linkers and Loaders](https://www.iecc.com/linker/) — глава 10
