# Режимы работы процессора и системные вызовы

## Кольца защиты

Архитектура x86 (с 80286, 1982) поддерживает **четыре уровня привилегий** — Ring 0..3. Дизайн унаследован от Multics, где более тонкая градация позволяла разделять ядро, драйверы, системные сервисы и user код. На практике большинство современных ОС (Linux, Windows NT, macOS, BSD) использует только два — Ring 0 и Ring 3.

```
        ┌─────────────────────────────────┐
        │           Ring 3                │  ← user code: libc, прикладные
        │  ┌───────────────────────────┐  │    программы
        │  │        Ring 2             │  │  ← (исторически: device drivers,
        │  │  ┌─────────────────────┐  │  │    не используется в Linux/NT)
        │  │  │      Ring 1         │  │  │  ← (исторически: OS services,
        │  │  │  ┌───────────────┐  │  │  │    Xen использовал для PV-гостей)
        │  │  │  │   Ring 0      │  │  │  │  ← kernel: полный доступ
        │  │  │  └───────────────┘  │  │  │
        │  │  └─────────────────────┘  │  │
        │  └───────────────────────────┘  │
        └─────────────────────────────────┘

         Современные ОС: только Ring 0 + Ring 3
         Гипервизор: Ring -1 (VMX root mode), SMM: Ring -2
```

**Ring 0 (kernel mode)** — режим, в котором выполняется код ядра ОС:

- полный доступ к физической памяти и всем устройствам;
- доступны все привилегированные инструкции (`cli`, `sti`, `hlt`, `lgdt`, `lidt`, `mov cr*`, `wrmsr`);
- ошибки → kernel panic (или Oops в Linux).

**Ring 3 (user mode)** — режим для пользовательского кода:

- нет прямого доступа к памяти ядра (kernel space помечен `U/S=0` в PTE);
- привилегированные инструкции → `#GP` → `SIGSEGV`;
- I/O порты — через `IOPL` поле RFLAGS (по умолчанию 0, доступ запрещён);
- доступ к защищённым ресурсам — через системные вызовы.

**Почему 2 кольца, а не 4**. Кольца реализованы через segmentation (CS.CPL, DPL дескрипторов сегментов), а **paging** (введён в 386) проверяет только `U/S` бит — два уровня. Использовать Ring 1/2 означало бы поддерживать сегментированную модель памяти параллельно с paging. UNIX-like ОС (и NT) выбрали flat-memory model + paging — двух колец достаточно для разделения user/kernel. В x86-64 long mode **сегментные limits игнорируются** для CS/DS/ES/SS, поэтому Ring 1/2 потеряли практический смысл совсем.

**Гипервизорные «отрицательные» кольца**:

- **Ring -1 (VMX root / SVM host)** — режим гипервизора (Intel VT-x: `VMXON`, AMD-V: `VMRUN`). Появился в 2005-2006. Гость думает что в Ring 0, но `vmexit` уводит управление в гипервизор.
- **Ring -2 (SMM — System Management Mode)** — самый привилегированный режим, недоступный ни ОС, ни гипервизору. Используется BIOS/UEFI для управления питанием, обработки thermal events, секретов ME. Вход — через `SMI` (System Management Interrupt), код живёт в зашифрованной SMRAM.
- **Ring -3** (неформально) — Intel ME / AMD PSP: отдельный сопроцессор с собственным ARC/ARM ядром, выполняется параллельно основному CPU.

### CPL / DPL / RPL

Текущий уровень привилегий — поле **CPL (Current Privilege Level)** в младших 2 битах селектора CS:

- CPL = 0: kernel mode (Ring 0);
- CPL = 3: user mode (Ring 3).

При доступе к сегменту проверяется правило: `max(CPL, RPL) ≤ DPL` (segment'у должно «хватить» привилегий). Это даёт защиту от обхода через подмену селектора в `mov ds, ax`.

- **CPL** — текущий уровень (из CS.RPL);
- **DPL (Descriptor Privilege Level)** — уровень доступа к самому сегменту (в его дескрипторе GDT/LDT);
- **RPL (Requested Privilege Level)** — заявленный уровень в загружаемом селекторе. Используется для `arpl` чтобы ядро не могло «случайно» доступиться к kernel-памяти через user-предоставленный pointer.

### IOPL

Поле **IOPL** (биты 12-13 в RFLAGS) определяет минимальный CPL, при котором разрешены инструкции `in`/`out` и работа с прерываниями (`cli`/`sti`). По умолчанию IOPL=0 — только Ring 0 может делать I/O. Linux может выдать IOPL=3 через системный вызов `iopl(2)` (требует `CAP_SYS_RAWIO`), что позволяет user-процессу напрямую работать с I/O портами (например, X server в legacy режиме).

### Кольца в других архитектурах

| ОС / ISA | Уровни |
|---|---|
| x86 protected | Ring 0..3 (Linux/Windows: 0 + 3) |
| x86 long mode + VMX | Ring -2 (SMM), Ring -1 (VMX root), Ring 0, Ring 3 |
| ARM AArch64 | EL0 (user) / EL1 (kernel) / EL2 (hypervisor) / EL3 (secure monitor) |
| RISC-V | M-mode (machine) / S-mode (supervisor) / U-mode (user); HS-mode для гипервизора |
| PowerPC | User / Supervisor / Hypervisor |

```
Переход Ring 3 → Ring 0 → Ring 3 при write(1, buf, len):

  User space (Ring 3)                 Kernel space (Ring 0)
  ─────────────────────────────       ──────────────────────────────────────
  mov  $1,  %rax    ; sys_write
  mov  $1,  %rdi    ; fd = stdout
  lea  buf, %rsi    ; buffer
  mov  $len,%rdx    ; length
  syscall  ────────────────────────▶  CPU аппаратно:
                                        rcx ← rip   (адрес возврата)
                                        r11 ← rflags
                                        CPL: 3 → 0
                                        rip ← MSR_LSTAR

                                      ┌──────────────────────────────┐
                                      │ entry_SYSCALL_64             │
                                      │   swapgs                     │
                                      │   переключить на kernel stack│
                                      │   push pt_regs               │
                                      │   call sys_write()           │
                                      │   pop  pt_regs               │
                                      │   rax ← return value/-errno  │
                                      └──────────────────────────────┘

                                      sysretq
  ; следующая инструкция     ◀──────    CPL: 0 → 3
  ; rax = результат                     rip    ← rcx
                                        rflags ← r11
```

## Системный вызов vs. обычная функция

| Аспект                     | Обычная функция            | Системный вызов                               |
|----------------------------|----------------------------|-----------------------------------------------|
| **Режим**                  | User → User                | User → Kernel → User                          |
| **Таблица переходов**      | Процессор прыгает напрямую | Через таблицу дескрипторов (IDT)              |
| **Переключение контекста** | Нет                        | Да (сохраняются регистры, стек)               |
| **Валидация аргументов**   | На совести программиста    | Ядро проверяет права доступа                  |
| **Скорость**               | Несколько тактов           | 100+ тактов                                   |
| **Возможные ошибки**       | Segfault                   | Permission denied, Bad file descriptor и т.д. |

## Инструкция syscall

`syscall` — специальная инструкция x86-64, которая переключает процессор из user mode в kernel mode и передаёт
управление точке входа в ядро. При выполнении `syscall`:

1. Адрес возврата (следующая инструкция) сохраняется в `rcx`;
2. Флаги `rflags` сохраняются в `r11`;
3. Адрес точки входа ядра загружается из MSR `LSTAR`;
4. Процессор переходит в kernel mode (CPL становится 0);
5. Управление передаётся обработчику в ядре.

Соглашение о вызовах для системных вызовов Linux x86-64:

- `rax` — номер системного вызова;
- `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` — аргументы (до 6);
- после возврата: `rax` — результат (или отрицательный код ошибки).

## Примеры использования syscall

Вызов `exit(0)` напрямую через ассемблерную вставку:

```c
#include <sys/syscall.h>

void exit_direct(void) {
    asm volatile(
        "movq $60, %%rax\n\t"   /* syscall 60 = exit */
        "movq $0,  %%rdi\n\t"   /* exit code = 0 */
        "syscall"
        : : : "rax", "rdi"
    );
}
```

Функция `read()` из libc является тонкой обёрткой над системным вызовом:

```c
ssize_t n = read(fd, buf, size);
```

Внутри libc это реализовано примерно так:

```asm
    movq $0,  %rax       # номер syscall: read = 0
    movq fd,  %rdi       # arg1: file descriptor
    movq buf, %rsi       # arg2: буфер
    movq size,%rdx       # arg3: количество байт
    syscall
    # результат в rax (количество прочитанных байт или -errno)
```

### Вызов execve через ассемблерную вставку

`execve` (syscall 59) заменяет текущий процесс новой программой. Это одна из немногих ситуаций, когда после успешного
системного вызова возврата нет:

```c
#include <stdio.h>

void execve_demo(void) {
    const char *filename = "/bin/ls";
    const char *argv[] = {"/bin/ls", "-la", "/tmp", NULL};
    const char *envp[] = {NULL};

    /* execve ЗАМЕНЯЕТ адресное пространство — после успеха сюда не вернуться */
    asm volatile(
        "movq $59, %%rax\n\t"   /* syscall 59 = execve */
        "movq %0, %%rdi\n\t"    /* arg1: filename */
        "movq %1, %%rsi\n\t"    /* arg2: argv */
        "movq %2, %%rdx\n\t"    /* arg3: envp */
        "syscall"
        :
        : "r"(filename), "r"(argv), "r"(envp)
        : "rax", "rdi", "rsi", "rdx"
    );

    /* При успехе сюда не дойдём; при ошибке rax < 0 */
    printf("execve failed\n");
}

int main(void) {
    execve_demo();
    return 0;
}
```

Рекомендуемый способ — использовать обёртку libc, которая сама формирует правильный системный вызов:

```c
#include <unistd.h>

int main(void) {
    const char *argv[] = {"/bin/ls", "-la", "/tmp", NULL};
    execve("/bin/ls", (char * const *)argv, NULL);
    perror("execve");   /* выполнится только при ошибке */
    return 1;
}
```

## Привилегированные инструкции

Привилегированные инструкции доступны только в kernel mode (CPL=0). Попытка выполнить их в user mode вызывает исключение
#GP (General Protection Fault), которое ОС превращает в сигнал SIGSEGV для процесса.

| Инструкция            | Назначение                                          |
|-----------------------|-----------------------------------------------------|
| `cli`, `sti`          | Запретить/разрешить аппаратные прерывания           |
| `lgdt`, `lidt`        | Загрузить GDT/IDT (таблицы дескрипторов)            |
| `ltr`                 | Загрузить Task Register                             |
| `mov cr0`–`cr4`       | Изменить control registers (paging, protected mode) |
| `wrmsr`, `rdmsr`      | Читать/писать Model-Specific Registers              |
| `hlt`                 | Остановить процессор до следующего прерывания       |
| `invlpg`              | Инвалидировать запись TLB                           |
| `sysenter`, `sysexit` | Быстрые переходы kernel/user на x86-32              |

```c
/* Попытка выполнить cli в user mode вызовет SIGSEGV */
void try_disable_interrupts(void) {
    asm volatile("cli");   /* General Protection Fault -> SIGSEGV */
}
```

Привилегированные регистры (доступны только в ring 0):

- `CR0` — управление paging, protected mode, write-protect;
- `CR2` — адрес, вызвавший page fault;
- `CR3` — физический адрес page directory текущего процесса;
- `CR4` — дополнительные флаги (PAE, PSE, VMXE и др.);
- `MSR` (Model-Specific Registers) — частота, температура, турбо-буст и др.

## vDSO (Virtual Dynamic Shared Object)

vDSO — механизм ядра Linux, который позволяет некоторым системным вызовам выполняться без переключения в kernel mode.
Ядро отображает небольшую страницу своего кода непосредственно в адресное пространство каждого процесса.

Системные вызовы, реализованные через vDSO на x86-64:

- `gettimeofday()` / `clock_gettime()` — чтение часов без `syscall`
- `getcpu()` — номер текущего процессора
- `time()` — секунды с эпохи Unix

Вместо полного переключения в kernel mode libc вызывает функцию прямо из отображённой страницы vDSO — это в 10–20 раз
быстрее обычного `syscall`.

```bash
cat /proc/self/maps | grep vdso
# 7ffd3e3f2000-7ffd3e3f4000 r-xp 00000000 00:00 0  [vdso]
```

Адрес ELF-заголовка vDSO можно получить через вспомогательный вектор ядра:

```c
#include <sys/auxv.h>
unsigned long vdso_base = getauxval(AT_SYSINFO_EHDR);
```

## Таблица основных системных вызовов x86-64 Linux

| Номер | Название   | Сигнатура                             |
|-------|------------|---------------------------------------|
| 0     | read       | `read(fd, buf, count)`                |
| 1     | write      | `write(fd, buf, count)`               |
| 2     | open       | `open(filename, flags, mode)`         |
| 3     | close      | `close(fd)`                           |
| 39    | getpid     | `getpid()`                            |
| 57    | fork       | `fork()`                              |
| 59    | execve     | `execve(filename, argv, envp)`        |
| 60    | exit       | `exit(status)`                        |
| 61    | wait4      | `wait4(pid, status, options, rusage)` |
| 231   | exit_group | `exit_group(status)`                  |

## Связанные темы

- [Прерывания](interrupts.md) — IDT, переход в kernel mode через прерывания
- [Встроенный ассемблер в GCC](inline_assembler_advanced.md) — инструкция `syscall` через `asm()`
- [Введение в системные вызовы](../assembler_and_processor/system_calls_introduction.md) — libc-обёртки, `strace`, errno
- [Основы ассемблера](assembler_basics.md) — регистры, используемые при системных вызовах

## Источники

- `man 2 syscalls` — полный список системных вызовов Linux
- `man 2 syscall` — низкоуровневый интерфейс syscall из libc
- `man 7 vdso` — описание механизма vDSO
- Linux Syscall Table: https://filippo.io/linux-syscall-table/
- Intel SDM Vol. 2: инструкции syscall, sysenter
- Linux kernel source: `arch/x86/entry/syscalls/syscall_64.tbl`
