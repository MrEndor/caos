# Динамический линковщик (ld.so) изнутри

Динамический линковщик — это пользовательская программа, которая запускается **раньше** вашей. Когда ядро
видит ELF с секцией `.interp`, оно загружает в адресное пространство процесса именно интерпретатор
(`/lib64/ld-linux-x86-64.so.2`), а не сам бинарь. Дальше всё — загрузка `.so`, релокации, разрешение
символов, инициализация TLS, вызов конструкторов — делает он, и только потом передаёт управление в `_start`
программы.

Этот промежуточный слой существует не ради красоты, а потому что иначе пришлось бы либо встраивать линковщик
в каждый бинарь (как при `-static`), либо требовать от ядра умения парсить ELF-таблицы релокаций.
ABI System V вынесло эту работу в отдельную shared-библиотеку — это решение определяет почти всю
архитектуру современных Linux-процессов.

## Цепочка запуска: от kernel до main

```
 execve("./prog", argv, envp)
       │
       ▼
 ┌──────────────────────────────────────────────────────────┐
 │  Kernel: fs/binfmt_elf.c                                 │
 │  • парсит ELF header программы                           │
 │  • mmap PT_LOAD сегментов (.text r-x, .data rw-, .bss)   │
 │  • читает PT_INTERP → строка "/lib64/ld-linux-x86-64..." │
 │  • открывает интерпретатор, mmap его PT_LOAD сегментов   │
 │  • строит начальный стек: argc, argv, envp, auxv         │
 │  • jmp ld.so::_start  (точка входа ИНТЕРПРЕТАТОРА)       │
 └─────────────────────────────┬────────────────────────────┘
                               │
                               ▼
 ┌──────────────────────────────────────────────────────────┐
 │  ld.so::_start  (sysdeps/x86_64/dl-machine.h)            │
 │  • сохраняет указатель на стек                           │
 │  • call _dl_start(stack_ptr)                             │
 └─────────────────────────────┬────────────────────────────┘
                               │
                               ▼
 ┌──────────────────────────────────────────────────────────┐
 │  _dl_start → _dl_start_final → dl_main                   │
 │  (elf/rtld.c — главная функция самого ld.so)             │
 │                                                          │
 │  1. self-relocation: ld.so правит СВОИ ЖЕ релокации,     │
 │     потому что его загрузил kernel без помощи линковщика │
 │  2. парсит auxv (AT_PHDR, AT_ENTRY, AT_BASE)             │
 │  3. строит link_map для главного бинаря                  │
 │  4. рекурсивно загружает DT_NEEDED (см. ниже)            │
 │  5. выполняет релокации каждого объекта                  │
 │  6. инициализирует TLS (initial-exec блоки)              │
 │  7. вызывает .init / .init_array всех .so в порядке      │
 │     обратной зависимости                                 │
 │  8. jmp на e_entry главного бинаря  ──▶ _start программы │
 └─────────────────────────────┬────────────────────────────┘
                               │
                               ▼
 ┌──────────────────────────────────────────────────────────┐
 │  prog::_start  (из crt1.o)                               │
 │  • достаёт argc/argv/envp со стека                       │
 │  • call __libc_start_main(main, argc, argv, ...)         │
 └─────────────────────────────┬────────────────────────────┘
                               │
                               ▼
 ┌──────────────────────────────────────────────────────────┐
 │  __libc_start_main  (libc.so)                            │
 │  • atexit-обработчики                                    │
 │  • .init_array программы (конструкторы C++)              │
 │  • call main(argc, argv, envp)                           │
 └──────────────────────────────────────────────────────────┘
```

Ключевая деталь — **self-relocation**. Ядро отображает `ld.so` в память, но никаких релокаций для него
не выполняет: некому, кроме самого ld.so. Поэтому первые несколько сотен инструкций `_dl_start` написаны
так, чтобы работать **без** обращений к глобальным переменным через GOT — иначе они бы упали по
неинициализированному адресу. Только после прохода по собственной `.rela.dyn` динамический линковщик
начинает пользоваться обычным C-кодом.

Посмотреть, какой именно интерпретатор зашит в бинарь:

```bash
readelf -l ./prog | grep interpreter
# [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
```

Запустить программу через явный вызов линковщика (полезно, если нужна другая версия glibc):

```bash
/lib64/ld-linux-x86-64.so.2 ./prog
/opt/glibc-2.39/lib/ld-linux-x86-64.so.2 --library-path /opt/glibc-2.39/lib ./prog
```

## Поиск символов: BFS по дереву зависимостей

Когда `ld.so` встречает релокацию вида «функция `foo` где-то в загруженных объектах», он не ищет её
наугад. Порядок поиска строго определён ABI и называется **scope lookup**.

```
 Глобальная scope-цепочка строится BFS-обходом DT_NEEDED:

       main_binary
        │ DT_NEEDED: libA.so, libB.so
        │
        ▼
   ┌─────────┐   ┌─────────┐
   │ libA.so │   │ libB.so │
   └────┬────┘   └────┬────┘
        │             │ DT_NEEDED: libC.so
        ▼             ▼
   ┌─────────┐   ┌─────────┐
   │ libC.so │   │ libC.so │  ← повторно не загружается,
   └─────────┘   └─────────┘    link_map один на объект
                      │
                      ▼ DT_NEEDED: libD.so
                 ┌─────────┐
                 │ libD.so │
                 └─────────┘

 Итоговый scope (порядок поиска символа):

  [ main_binary, libA.so, libB.so, libC.so, libD.so ]
       ▲
       │
   первый, у кого STT_FUNC|STT_OBJECT с нужным именем
   и binding != STB_LOCAL — побеждает
```

Алгоритм:

1. В очередь кладётся главный бинарь.
2. Из очереди извлекается объект, в результирующий список добавляются все его `DT_NEEDED`, ещё не
   загруженные ранее.
3. Шаг 2 повторяется, пока очередь не опустеет.
4. При резолве символа `ld.so` идёт по этому списку слева направо. Первое определение с подходящим
   binding (`STB_GLOBAL` или `STB_WEAK`) выигрывает.

Это объясняет много неинтуитивного. Например, если `libA.so` и `libB.so` обе экспортируют функцию
`init`, и в `DT_NEEDED` главного бинаря сначала `libA`, то вызов `init` из любой `.so` (включая `libB`!)
уйдёт в реализацию из `libA`. Это называется **symbol interposition** и иногда приводит к тонким багам.

Отдельный flag `DF_SYMBOLIC` (`-Bsymbolic` при сборке) меняет поведение: вызовы внутри `libB.so`
сначала ищутся в самой `libB.so`, и только потом — в глобальном scope. Полезно для библиотек, которые
не хотят, чтобы их внутренности перехватывались.

## LD_PRELOAD: вставка в начало scope

`LD_PRELOAD` — это самый простой механизм инверсии вышеописанного алгоритма. Библиотеки из этой
переменной добавляются в scope **перед** главным бинарём:

```
 Без LD_PRELOAD:                       С LD_PRELOAD=libfake.so:

 [ main, libA, libB, libc ]            [ libfake, main, libA, libB, libc ]
   ▲                                     ▲
   │ malloc ищется здесь                 │ malloc ищется здесь
   │ → libc.so (первое                   │ → libfake.so (если она экспортирует
   │   определение)                      │   malloc)
```

Поэтому через `LD_PRELOAD` можно подменить `malloc`, `read`, `write`, `connect` — что угодно из libc.
Это основа профилировщиков (`tcmalloc`, `jemalloc` через preload), трассировки (`ltrace`), тестовых
заглушек, инструментов вроде `proot`, и, к сожалению, многих rootkit'ов.

Классический пример — перехват `malloc`:

```c
// fake_malloc.c — gcc -shared -fPIC -ldl fake_malloc.c -o libfake.so
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

void *malloc(size_t size) {
    static void *(*real_malloc)(size_t) = NULL;
    if (!real_malloc)
        real_malloc = dlsym(RTLD_NEXT, "malloc");

    void *p = real_malloc(size);
    fprintf(stderr, "malloc(%zu) = %p\n", size, p);
    return p;
}
```

```bash
LD_PRELOAD=./libfake.so ./prog
```

`RTLD_NEXT` — псевдо-handle для `dlsym`, означающий «найди ту же функцию, но в следующем элементе
scope после моей библиотеки». Без него `dlsym(RTLD_DEFAULT, "malloc")` вернул бы указатель на нас же
самих, и получилась бы бесконечная рекурсия.

`LD_PRELOAD` (как и большинство `LD_*`) игнорируется для **setuid**-бинарей — иначе любой пользователь
мог бы получить root, подсунув свою `libc`. Глубже это контролируется флагом `AT_SECURE` в auxv.

## Пути поиска .so

Когда у `ld.so` есть имя `libfoo.so.1`, он ищет его в строго определённом порядке:

| Шаг | Источник                                      | Когда используется                 |
|-----|-----------------------------------------------|------------------------------------|
| 1   | `DT_RPATH` из ELF (если нет `DT_RUNPATH`)     | устаревший тег, применяется первым |
| 2   | `LD_LIBRARY_PATH`                             | игнорируется для setuid-бинарей    |
| 3   | `DT_RUNPATH` из ELF                           | современный аналог `DT_RPATH`      |
| 4   | `/etc/ld.so.cache`                            | сгенерирован `ldconfig`            |
| 5   | `/lib`, `/usr/lib` (и `/lib64`, `/usr/lib64`) | hardcoded в `ld.so`                |

В `DT_RPATH`/`DT_RUNPATH` поддерживаются спецсимволы:

- `$ORIGIN` — каталог, где лежит сам ELF-файл, делающий запрос.
- `$LIB` — `lib`, `lib32` или `lib64` в зависимости от ABI.
- `$PLATFORM` — например, `x86_64`.

```bash
# Зашить относительный rpath, чтобы бинарь искал .so рядом с собой
g++ main.cpp -L. -lmy -Wl,-rpath,'$ORIGIN' -o prog

readelf -d prog | grep PATH
# 0x000000000000001d (RUNPATH)  Library runpath: [$ORIGIN]
```

`$ORIGIN` решает проблему «программа лежит в `/opt/myapp/bin`, библиотеки в `/opt/myapp/lib`» без
переменных окружения и установки в системные пути.

## ld.so cache

Сканировать `/lib`, `/usr/lib` и десятки других каталогов при каждом запуске каждой программы — это
тысячи `open`/`stat`. Поэтому существует **бинарный кэш** `/etc/ld.so.cache`, который перечисляет все
известные `.so` с их полными путями:

```
              /etc/ld.so.conf
              /etc/ld.so.conf.d/*.conf
                       │
                       │   ldconfig (требует root)
                       ▼
              /etc/ld.so.cache    (бинарный hash-table)
                       │
                       │   читается каждым ld.so при старте
                       ▼
              быстрый ответ: "libfoo.so.1 → /opt/mylibs/libfoo.so.1.2.3"
```

Что внутри:

```bash
ldconfig -p | head
# 1234 libs found in cache `/etc/ld.so.cache'
#   libz.so.1 (libc6,x86-64) => /usr/lib/libz.so.1
#   libssl.so.3 (libc6,x86-64) => /usr/lib/libssl.so.3
#   ...

ldconfig -p | grep libcrypto
```

Чтобы добавить новый каталог:

```bash
echo /opt/mylibs > /etc/ld.so.conf.d/mylibs.conf
ldconfig
```

`ldconfig` также создаёт symlink'и вида `libfoo.so.1 → libfoo.so.1.2.3` на основе `SONAME` библиотеки —
это нужно для версионирования (см. `man 8 ldconfig`).

## dlopen / dlsym / dlclose

Динамический линковщик умеет загружать `.so` не только при старте, но и в любой момент жизни процесса.
Это интерфейс `<dlfcn.h>`:

```c
#include <dlfcn.h>

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);
int   dlclose(void *handle);
char *dlerror(void);
```

Флаги `dlopen` определяют, как новый объект встраивается в scope:

| Флаг            | Эффект                                                                   |
|-----------------|--------------------------------------------------------------------------|
| `RTLD_LAZY`     | релокации функций откладываются до первого вызова                        |
| `RTLD_NOW`      | все релокации немедленно (как `LD_BIND_NOW`)                             |
| `RTLD_GLOBAL`   | символы из загружаемой `.so` видны последующим `dlopen` и всему процессу |
| `RTLD_LOCAL`    | символы видны только через возвращённый handle (по умолчанию)            |
| `RTLD_NODELETE` | при `dlclose` не выгружать                                               |
| `RTLD_NOLOAD`   | не загружать, только вернуть handle если уже загружена                   |

```
 dlopen("libplugin.so", RTLD_LAZY | RTLD_LOCAL)
           │
           ▼
 ┌──────────────────────────────────────────┐
 │  1. _dl_open в ld.so                     │
 │  2. ищет файл (rpath, LD_LIBRARY_PATH,   │
 │     cache, default paths)                │
 │  3. mmap PT_LOAD сегментов               │
 │  4. рекурсивно догружает её DT_NEEDED    │
 │  5. локальная scope-цепочка для handle:  │
 │     [libplugin, его зависимости]         │
 │  6. релокации (lazy для функций)         │
 │  7. инициализация TLS, если нужна        │
 │  8. .init_array библиотеки               │
 │  9. возвращает handle                    │
 └──────────────────────────────────────────┘
           │
           ▼
 handle ──▶ dlsym(handle, "plugin_entry")
                  │
                  ▼ ищет в локальной scope handle'а
            void (*entry)(void) = ...
            entry();
           │
           ▼
 dlclose(handle)
           │
           ▼ счётчик ссылок --
   если 0:
     .fini_array библиотеки
     munmap её сегментов
     удаление из link_map
```

Подводный камень `dlclose` — если символы из библиотеки уже сохранены где-то в указателях, после
выгрузки они становятся «висячими», и обращение к ним даёт `SIGSEGV` в `??` без полезного стека. Поэтому
многие фреймворки (Qt plugin loader, glib GModule) используют `RTLD_NODELETE`: библиотека остаётся в
памяти даже после `dlclose`.

`dlsym(RTLD_DEFAULT, "name")` ищет символ в глобальном scope процесса, `dlsym(RTLD_NEXT, "name")` —
после текущей библиотеки. Эти псевдо-handle'ы — основа техники LD_PRELOAD-перехвата.

## TLS: initial-exec vs general-dynamic

Thread-local storage — память, у которой есть отдельная копия для каждого потока. На x86-64 эта область
адресуется через сегментный регистр `fs`: переменная `__thread int counter` транслируется компилятором
в обращение к `fs:[offset]`, где `offset` — смещение внутри TLS-блока текущего потока.

`ld.so` выделяет TLS-блок при старте и при каждом `pthread_create`. Размещение зависит от модели:

```
 Static TLS (initial-exec model) — для главного бинаря и .so,
 загруженных при старте:

 Поток N:
                  ┌──────────────────────┐
   fs:0x0  ────▶  │  TCB (thread ctrl)   │  (содержит указатель сам на себя)
                  ├──────────────────────┤
   fs:-N0  ────▶  │  TLS блок main exe   │  (смещение известно при линковке)
                  ├──────────────────────┤
   fs:-N1  ────▶  │  TLS блок libA.so    │
                  ├──────────────────────┤
   fs:-N2  ────▶  │  TLS блок libB.so    │
                  └──────────────────────┘

 Динамическая TLS (general-dynamic) — для .so, загруженных через dlopen:

   __tls_get_addr({ module_id, offset })
        │
        ▼
   DTV (Dynamic Thread Vector) — массив указателей на TLS-блоки
        │
        ▼
   при первом обращении — lazy-аллокация через calloc
```

Модели TLS, от самой быстрой к самой гибкой:

| Модель            | Когда применима                 | Стоимость доступа                  |
|-------------------|---------------------------------|------------------------------------|
| `local-exec`      | переменная в главном бинаре     | 1 инструкция: `mov fs:offset, …`   |
| `initial-exec`    | переменная в `.so`, загружен.   | 2 инструкции: GOT + `mov fs:…`     |
|                   | при старте                      |                                    |
| `local-dynamic`   | несколько TLS в одной `.so`     | вызов `__tls_get_addr`             |
| `general-dynamic` | переменная в dlopen-нутой `.so` | вызов `__tls_get_addr`, DTV lookup |

Размер initial-exec блока должен быть известен до запуска первого потока — иначе пришлось бы переразмечать
уже выделенный TLS. Из-за этого статический TLS имеет лимит (`DTV_SURPLUS`, ~1664 байт сверх блоков
обязательных библиотек), и если `dlopen` пытается загрузить `.so` с большим `PT_TLS`-сегментом и initial-exec
переменными, получается ошибка вида `cannot allocate memory in static TLS block`.

## dlopen audit API: la_objopen, la_symbind64

`ld.so` поддерживает hook-механизм для трассировки и аудита — **rtld-audit API**. Через переменную
окружения `LD_AUDIT` можно указать `.so`, которая получит уведомления о каждой загрузке объекта и о
каждом разрешении символа:

```c
// audit.c — gcc -shared -fPIC audit.c -o libaudit.so
#define _GNU_SOURCE
#include <link.h>
#include <stdio.h>
#include <string.h>

unsigned int la_version(unsigned int v) { return LAV_CURRENT; }

unsigned int la_objopen(struct link_map *map, Lmid_t lmid, uintptr_t *cookie) {
    fprintf(stderr, "[audit] loaded: %s\n", map->l_name);
    return LA_FLG_BINDFROM | LA_FLG_BINDTO;  // хотим перехватывать символы
}

uintptr_t la_symbind64(Elf64_Sym *sym, unsigned int ndx,
                       uintptr_t *refcook, uintptr_t *defcook,
                       unsigned int *flags, const char *symname) {
    fprintf(stderr, "[audit] bind: %s\n", symname);
    return sym->st_value;
}
```

```bash
LD_AUDIT=./libaudit.so ./prog
# [audit] loaded: /lib64/ld-linux-x86-64.so.2
# [audit] loaded: ./prog
# [audit] loaded: /lib/x86_64-linux-gnu/libc.so.6
# [audit] bind: __libc_start_main
# [audit] bind: printf
# ...
```

Аудитор запускается в **отдельном namespace** (`Lmid_t`), у него своя scope-цепочка, чтобы трассировка
функций самого аудитора не вызвала бесконечную рекурсию. Этот API лежит в основе таких инструментов,
как `latrace` и `sotruss`.

## LD_DEBUG: внутренняя диагностика

`LD_DEBUG` заставляет `ld.so` печатать в stderr подробный лог о своей работе. Категории:

| Значение     | Что показывает                                   |
|--------------|--------------------------------------------------|
| `libs`       | поиск и загрузка `.so` (какие пути перебираются) |
| `files`      | каждый mmap'нутый файл и его сегменты            |
| `symbols`    | каждый поиск символа в scope                     |
| `bindings`   | какому объекту разрешается каждый символ         |
| `reloc`      | выполняемые релокации                            |
| `versions`   | проверки версий символов (verneed/verdef)        |
| `scopes`     | scope-цепочки для каждого объекта                |
| `statistics` | сводка по числу релокаций и затраченному времени |
| `all`        | всё перечисленное                                |
| `help`       | вывести список и выйти                           |

```bash
LD_DEBUG=libs ./prog 2>&1 | head -20
LD_DEBUG=symbols ./prog 2>&1 | grep printf
LD_DEBUG=bindings,statistics ./prog
```

Полезные сценарии:

- Программа падает с `undefined symbol`, но `.so` вроде бы загружается — `LD_DEBUG=symbols` покажет,
  где именно ld.so ищет имя и почему не находит.
- Запуск тормозит — `LD_DEBUG=statistics` покажет, сколько релокаций и времени уходит на старт.
- Загружается «не та» версия библиотеки — `LD_DEBUG=libs` напечатает порядок поиска и какой путь
  выиграл.

`LD_DEBUG_OUTPUT=file` перенаправит вывод в указанный файл (с PID в имени), чтобы не мешать
stderr программы.

## ldd: что он на самом деле делает

`ldd` — это не отдельная утилита, а shell-скрипт (или маленький бинарь, в зависимости от libc), который
запускает `ld.so` с переменной `LD_TRACE_LOADED_OBJECTS=1`:

```bash
LD_TRACE_LOADED_OBJECTS=1 ./prog
# то же самое, что
ldd ./prog
```

В этом режиме `ld.so` не передаёт управление в `_start` программы. Он только проходит по `DT_NEEDED`,
печатает результат и выходит.

Опасность: `ldd ./untrusted_binary` для бинаря с подменённым `.interp` (например, на свою фейковую `.so`)
может **выполнить произвольный код**. Безопаснее использовать `objdump -p prog | grep NEEDED` или
`readelf -d prog`, которые только читают ELF-файл, не запуская его.

## Связанные темы

- [Статическая и динамическая линковка](static_dynamic_linking.md) — `LD_LIBRARY_PATH`, `LD_PRELOAD`, rpath, кэш
  `ldconfig`
- [PLT/GOT и lazy binding](plt_got.md) — что именно делают релокации, которые расставляет ld.so
- [Запуск и завершение программы](program_startup_shutdown.md) — место ld.so в цепочке execve → main
- [Формат ELF](elf_format.md) — секция `.interp`, `.dynamic`, `DT_NEEDED`
- [Символы и манглирование](symbols_and_mangling.md) — какие символы попадают в `.dynsym`

## Источники

- `man ld.so` — переменные окружения, порядок поиска, аудит-интерфейс
- `man 3 dlopen`, `man 3 dlsym` — динамическая загрузка из кода
- `man 8 ldconfig` — управление кэшем `/etc/ld.so.cache`
- [glibc source: elf/rtld.c, elf/dl-open.c, elf/dl-lookup.c](https://sourceware.org/git/?p=glibc.git)
- [System V ABI: Program Loading and Dynamic Linking](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- John R. Levine, [Linkers and Loaders](https://www.iecc.com/linker/) — глава 10 «Dynamic Linking and Loading»
- Ulrich Drepper, [How To Write Shared Libraries](https://www.akkadia.org/drepper/dsohowto.pdf)
- [ELF TLS specification](https://www.akkadia.org/drepper/tls.pdf) — Drepper о моделях TLS
