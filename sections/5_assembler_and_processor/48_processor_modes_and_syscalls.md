##### Какие есть режимы работы у процессора и чем они отличаются?

**Два основных режима:**

1. **User Mode (пользовательский режим)**
    - Программа имеет ограниченный доступ к ресурсам;
    - Нельзя напрямую обращаться к памяти ядра;
    - Нельзя выполнять привилегированные инструкции;
    - Для доступа к защищённым ресурсам нужно попросить ядро (через syscall).

2. **Kernel Mode (режим ядра)**
    - Полный доступ ко всем ресурсам;
    - Можно выполнять привилегированные инструкции;
    - Выполняется код ядра ОС.

**Сохранение режима:** в регистре флагов `RFLAGS` бит CPL (Current Privilege Level):

- CPL = 0: kernel mode;
- CPL = 3: user mode.

##### Чем принципиально отличается вызов сисколла от вызова обычной функции?

| Аспект                     | Обычная функция            | Системный вызов                               |
|----------------------------|----------------------------|-----------------------------------------------|
| **Режим**                  | User → User                | User → Kernel → User                          |
| **Таблица переходов**      | Процессор прыгает напрямую | Через таблицу дескрипторов (IDT)              |
| **Переключение контекста** | Нет                        | Да (сохраняются регистры, стек)               |
| **Валидация аргументов**   | На совести программиста    | Ядро проверяет права доступа                  |
| **Скорость**               | Несколько тактов           | 100+ тактов                                   |
| **Возможные ошибки**       | Segfault                   | Permission denied, Bad file descriptor и т.д. |

##### Что делает ассемблерная инструкция syscall?

**syscall** (на x86-64):

1. **Сохраняет адрес возврата** (в RCX);
2. **Загружает точку входа ядра** (из MSR_LSTAR);
3. **Переключает режим** CPU в kernel mode;
4. **Прыгает в ядро** на адрес точки входа;
5. **Меняет стек** на kernel stack.

**Поля в syscall:**

- `RAX` -- номер syscall'а (например, 59 для execve);
- `RDI, RSI, RDX, RCX, R8, R9` -- аргументы (система V AMD64 ABI);
- **После возврата:** `RAX` -- результат (или -1 в случае ошибки).

**Важно:** syscall сохраняет RCX (адрес возврата) и R11 (флаги), но они перезаписываются при выходе.

##### Покажите пример кода, где она встречается.

```c
#include <unistd.h>
#include <sys/syscall.h>

// Вызов exit(0) напрямую через syscall
void exit_syscall() {
    syscall(SYS_exit, 0);
    // или напрямую через ассемблер:
    asm volatile("movq $60, %%rax\n\t"      // syscall 60 = exit
                 "movq $0, %%rdi\n\t"       // arg1: exit code
                 "syscall"
                 : : : "rax", "rdi");
}
```

**В реальном коде:**

```c
#include <unistd.h>

// Обёртка libc, которая вызывает syscall внутри
ssize_t n = read(fd, buf, size);
```

Реально происходит:

```asm
mov $0, %eax            # номер syscall (read = 0)
mov fd, %edi            # arg1
mov buf, %rsi           # arg2
mov size, %rdx          # arg3
syscall
```

##### Покажите, как сделать сисколл execve напрямую через ассемблерную вставку.

```c
#include <stdio.h>
#include <string.h>

// Выполнить: /bin/ls -la /tmp
void execve_demo() {
    const char *filename = "/bin/ls";
    const char *argv[] = {"/bin/ls", "-la", "/tmp", NULL};
    const char *envp[] = {NULL};
    
    // ВАЖНО: execve ЗАМЕНЯЕТ адресное пространство текущего процесса!
    // Код после syscall никогда не выполнится
    asm volatile(
        "mov $59, %%rax\n\t"           // syscall 59 = execve
        "mov %0, %%rdi\n\t"            // arg1: filename
        "mov %1, %%rsi\n\t"            // arg2: argv (указатель на массив)
        "mov %2, %%rdx\n\t"            // arg3: envp (указатель на массив)
        "syscall"
        :
        : "r"(filename), "r"(argv), "r"(envp)
        : "rax", "rdi", "rsi", "rdx"
    );
    
    // Если execve успешна, точка сюда не достигается
    // Если ошибка, вернётся в RAX отрицательный код ошибки
    printf("execve failed\n");
}

int main() {
    execve_demo();
    return 0;
}
```

**Вывод программы:** выполнится `ls -la /tmp` (процесс заменится).

**Альтернативный способ с использованием libc (рекомендуется):**

```c
#include <unistd.h>

int main() {
    const char *argv[] = {"/bin/ls", "-la", "/tmp", NULL};
    const char *envp[] = {NULL};
    
    // libc обёртка над syscall execve
    execve("/bin/ls", (char * const *)argv, (char * const *)envp);
    
    // Код ниже выполнится только при ошибке
    perror("execve");
    return 1;
}
```

##### Что такое привилегированные инструкции процессора и что к ним относится?

**Привилегированные инструкции** -- инструкции, которые можно выполнять только в kernel mode (CPL=0). Попытка выполнить
их в user mode вызывает `#GP (General Protection) fault`.

**Примеры привилегированных инструкций:**

| Инструкция            | Назначение                                                 |
|-----------------------|------------------------------------------------------------|
| **cli, sti**          | Отключить/включить аппаратные прерывания                   |
| **lgdt, lidt, lldt**  | Загрузить GDT/IDT (таблицы дескрипторов)                   |
| **ltr**               | Загрузить Task Register                                    |
| **mov к CR0-CR4**     | Изменить control registers (paging, protected mode)        |
| **mov к MSR**         | Изменить Model-Specific Registers (турбо, частота)         |
| **hlt**               | Остановить процессор (до следующего прерывания)            |
| **invlpg**            | Инвалидировать запись в TLB (Translation Lookaside Buffer) |
| **wrmsr, rdmsr**      | Читать/писать в Model-Specific Registers                   |
| **sysenter, sysexit** | Быстрые входы в ядро (альтернатива syscall на x86-32)      |

**Пример:**

```c
// ОПАСНО! Будет segfault если запустить в user mode
void disable_interrupts() {
    asm volatile("cli");  // отключить прерывания
    // General Protection Fault (#GP) → SIGSEGV
}
```

**Привилегированные регистры:**

- **CR0** -- control register (включение paging, protected mode, и т.д.);
- **CR2** -- page fault linear address (куда прыгнуть при page fault);
- **CR3** -- page directory base (физический адрес page table текущего процесса);
- **CR4** -- дополнительные флаги управления;
- **MSR** (Model-Specific Registers) -- периферия (турбо, частота, температура и т.д.).

**Защита:**

- Только ядро ОС может выполнять привилегированные инструкции;
- Программы используют syscall для доступа к защищённым ресурсам;
- Попытка выполнить привилегированную инструкцию в user mode → процесс убивается с SIGSEGV.

---

## Таблица основных syscall'ов x86-64 Linux:

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

**Полный список:** `man 2 syscalls` или https://filippo.io/linux-syscall-table/
