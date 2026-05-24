# Раздел 5: Ассемблер и процессор

Этот раздел посвящён работе процессора x86-64 — от машинного уровня до тонкостей
аппаратной оптимизации. Вы разберётесь, как компилятор превращает C-код в инструкции,
как эти инструкции выполняются на суперскалярном конвейере и что происходит внутри
процессора при обращении к памяти, ветвлении или системном вызове.

Раздел охватывает несколько взаимосвязанных пластов:

- **Язык ассемблера** — синтаксис AT&T и Intel, регистры, соглашение о вызовах
  System V AMD64 ABI, стековые фреймы, отладка в GDB.
- **Взаимодействие C и ассемблера** — inline-asm, constraints, rdtsc, constant-time
  алгоритмы, интеграция отдельных `.s`-файлов в проект.
- **Безопасность** — переполнение буфера и защита от него: stack canary, ASLR, NX,
  RELRO.
- **Архитектурные оптимизации** — кэши, предсказание ветвлений, параллелизм
  инструкций (ILP), SIMD/AVX.
- **Привилегированный режим** — кольца защиты, инструкция `syscall`, vDSO,
  таблица прерываний (IDT), таймерное прерывание как основа многозадачности.

## Темы

| Страница | Что рассматривается |
|----------|---------------------|
| [Основы ассемблера](assembler_basics.md) | Регистры x86-64, AT&T/Intel синтаксис, базовые инструкции, соглашение о вызовах |
| [Условные переходы и флаги](conditional_jumps_flags.md) | Регистр RFLAGS, `cmp`/`test`, все формы `jcc`, `cmovcc`, реализация if/while/for |
| [Ассемблерные функции и GDB](inline_assembler.md) | Функции в отдельных `.s`-файлах, компоновка с C, отладка на уровне инструкций |
| [Стековые фреймы и вызовы функций](stack_frames_and_function_calls.md) | Инструкции `call`/`ret`, пролог/эпилог, System V AMD64 ABI, frame pointer |
| [Защита от переполнения буфера](buffer_overflow_protection.md) | Stack canary, ASLR, NX/DEP, RELRO, PIE, безопасные функции |
| [Кэши процессора](cpu_caches.md) | Иерархия L1/L2/L3, кэш-линии, пространственная локальность, false sharing |
| [Предсказание ветвлений](branch_prediction.md) | Branch predictor, mispredict penalty, `[[likely]]`/`__builtin_expect`, `cmov` |
| [Параллелизм на уровне инструкций](instruction_level_parallelism.md) | Pipeline, суперскалярность, out-of-order execution, Spectre/Meltdown |
| [Векторные инструкции (SIMD)](vector_instructions_simd.md) | SSE/AVX/AVX-512, XMM/YMM регистры, intrinsics, автовекторизация |
| [Встроенный ассемблер в GCC](inline_assembler_advanced.md) | `asm()`, constraints, volatile, rdtsc, constant-time сравнение |
| [Режимы процессора и системные вызовы](processor_modes_and_syscalls.md) | Ring 0/3, инструкция `syscall`, MSR LSTAR, vDSO, привилегированные инструкции |
| [Системные вызовы (введение)](system_calls_introduction.md) | Переход user → kernel; SysV AMD64 calling convention; `strace`; `errno`; прямой вызов через `syscall(2)` |
| [Прерывания](interrupts.md) | IDT, аппаратные прерывания (IRQ), исключения процессора, вытесняющая многозадачность |
| [vDSO](vdso.md) | Virtual Dynamic Shared Object; `clock_gettime` без syscall; вспомогательный вектор `AT_SYSINFO_EHDR`; vvar |
