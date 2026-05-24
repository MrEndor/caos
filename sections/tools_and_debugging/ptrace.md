# ptrace: как один процесс владеет другим

`ptrace(2)` — древний syscall, появившийся ещё в Unix V7 в 1979 году. Через него один процесс (**tracer**) получает
почти неограниченную власть над другим (**tracee**): читает и пишет его память, меняет регистры, перехватывает каждый
syscall, останавливает на каждой инструкции, подменяет аргументы и результаты. На этом единственном syscall построены
`gdb`, `strace`, `ltrace`, CRIU, Firejail, `gVisor`, libinjector, `graftcp` и десятки других инструментов.

Цена этой власти — огромный overhead. Каждое наблюдаемое событие — это десятки переключений контекста tracer ↔ kernel ↔
tracee. Для syscall-heavy нагрузки замедление достигает 100× и более. Поэтому современные инструменты наблюдаемости (
eBPF, perf, ftrace) обходят ptrace полностью — они работают на стороне kernel'а и не требуют остановки tracee. Но там,
где нужно именно вмешательство в выполнение — установка breakpoint'а, инжекция кода, подмена аргумента syscall —
альтернатив ptrace до сих пор нет.

## API

Весь интерфейс — один syscall с переменным значением первого аргумента:

```c
#include <sys/ptrace.h>
long ptrace(enum __ptrace_request request, pid_t pid, void *addr, void *data);
```

Семантика остальных трёх аргументов зависит от `request`. Возвращаемое значение — обычно 0 или прочитанное слово; ошибка
сигнализируется через `errno`, но `-1` может быть и валидным значением, поэтому `errno` нужно сбрасывать перед вызовом
`PTRACE_PEEK*`.

Основные запросы:

| Request                  | Кто вызывает | Назначение                                              |
|--------------------------|--------------|---------------------------------------------------------|
| `PTRACE_TRACEME`         | tracee       | «я готов быть трассированным» — после fork, перед exec  |
| `PTRACE_ATTACH`          | tracer       | присоединиться к существующему PID, послать SIGSTOP     |
| `PTRACE_SEIZE` (≥3.4)    | tracer       | attach без доставки SIGSTOP — чище, для modern tooling  |
| `PTRACE_DETACH`          | tracer       | отвязаться, tracee продолжает работать                  |
| `PTRACE_CONT`            | tracer       | продолжить tracee до следующего сигнала                 |
| `PTRACE_SYSCALL`         | tracer       | продолжить, остановиться на входе/выходе syscall        |
| `PTRACE_SINGLESTEP`      | tracer       | выполнить одну инструкцию и остановиться                |
| `PTRACE_PEEKTEXT/DATA`   | tracer       | прочитать word из памяти tracee                         |
| `PTRACE_PEEKUSER`        | tracer       | прочитать word из `struct user` (включая регистры)      |
| `PTRACE_POKETEXT/DATA`   | tracer       | записать word в память tracee                           |
| `PTRACE_POKEUSER`        | tracer       | записать word в `struct user`                           |
| `PTRACE_GETREGS/SETREGS` | tracer       | все GP-регистры через `struct user_regs_struct`         |
| `PTRACE_GETREGSET`       | tracer       | extended state (NT_PRSTATUS, NT_PRFPREG, NT_X86_XSTATE) |
| `PTRACE_SETREGSET`       | tracer       | записать тот же набор                                   |
| `PTRACE_INTERRUPT`       | tracer       | попросить tracee остановиться (только после SEIZE)      |
| `PTRACE_SETOPTIONS`      | tracer       | включить TRACEFORK/CLONE/EXEC/SECCOMP/EXITKILL          |
| `PTRACE_GETSIGINFO`      | tracer       | прочитать `siginfo_t` доставляемого сигнала             |
| `PTRACE_SETSIGINFO`      | tracer       | подменить `siginfo_t` (или подавить, передав 0)         |
| `PTRACE_GETEVENTMSG`     | tracer       | данные PTRACE_EVENT_* (новый PID при fork, exit code)   |

Семантика `addr`/`data` зависит от request. У `PEEK*` слово возвращается, у `POKE*` записывается из `data`; у `GETREGS`
указатель на буфер передаётся через `data`, а `addr` игнорируется. Такая несогласованность — наследие 70-х: syscall
задумывался для маленького CRT-терминала с adb-отладчиком.

## Tracer/tracee и состояния

Любой процесс в Linux после attach всегда находится в одном из четырёх состояний:

- **running** — обычное выполнение, как у нетрассированного процесса
- **ptrace-stop** — kernel остановил tracee и ждёт реакции tracer'а
- **group-stop** — job-control остановка (SIGSTOP/SIGTSTP), не связана с ptrace напрямую
- **exited / zombie** — завершён, ждёт `wait()` от parent

```
                                ┌──────────────────────────┐
                                │       tracee state       │
                                └──────────────────────────┘

         ┌─────────────────────────────────────────────────────────────┐
         │                                                             │
         ▼                                                             │
     ┌─────────┐    syscall/signal/event       ┌──────────────────┐    │
     │ running │ ─────────────────────────────▶│   ptrace-stop    │    │
     │         │                               │ (kernel ждёт TR) │    │
     │         │ ◀─────────────────────────────│                  │    │
     └─────────┘   PTRACE_CONT / SYSCALL       └────────┬─────────┘    │
         │                                              │              │
         │              SIGSTOP                         │              │
         │           (job control)                      │              │
         ▼                                              │              │
     ┌─────────┐                                        │              │
     │ group-  │                                        │              │
     │  stop   │ ───────────────────────────────────────┘              │
     │         │      SIGCONT (или PTRACE_LISTEN после SEIZE)          │
     └────┬────┘                                                       │
          │                                                            │
          └────────────────────────────────────────────────────────────┘
                                  │
                                  │  exit(), kill(SIGKILL)
                                  ▼
                              ┌─────────┐
                              │ exited  │
                              └─────────┘
```

Tracer узнаёт о каждом stop через `waitpid()` — wait возвращает PID остановленного потока, а `status` через макросы
`WIFSTOPPED`/`WSTOPSIG` сообщает причину. Это означает, что весь API ptrace построен на парах **«дать команду
продолжить → дождаться следующего stop через wait → прочитать состояние»**.

## Классификация stops

Под общим именем «ptrace-stop» скрывается несколько разных событий, и tracer обязан различать их, чтобы реагировать
правильно.

```
                          ┌──────── tracee остановлен ────────┐
                          │                                   │
                          ▼                                   ▼
                   ┌────────────┐                     ┌──────────────┐
                   │ group-stop │                     │ ptrace-stop  │
                   │ (SIGSTOP,  │                     │              │
                   │  SIGTSTP)  │                     │              │
                   └────────────┘                     └──────┬───────┘
                          ▲                                  │
                          │                                  │
                  это не ptrace,         ┌──────────────────┼──────────────────────┐
                  это job control        │                  │                      │
                                         ▼                  ▼                      ▼
                                ┌─────────────────┐ ┌─────────────────┐ ┌────────────────────┐
                                │ signal-delivery │ │   syscall-stop  │ │  PTRACE_EVENT_*    │
                                │      stop       │ │  enter / exit   │ │ FORK/CLONE/EXEC/   │
                                │                 │ │                 │ │ EXIT/SECCOMP/STOP  │
                                │ tracee получил  │ │ при входе и     │ │                    │
                                │ сигнал, kernel  │ │ выходе syscall  │ │ событие, на которое│
                                │ дал tracer'у    │ │ (PTRACE_SYSCALL │ │ подписался через   │
                                │ право решить    │ │  включён)       │ │ PTRACE_SETOPTIONS  │
                                └─────────────────┘ └─────────────────┘ └────────────────────┘

                                          + single-step-stop (после PTRACE_SINGLESTEP)
```

**signal-delivery-stop**. Tracee получает любой сигнал (кроме SIGKILL — этот не остановить). Kernel ставит tracee в
ptrace-stop ДО доставки и сообщает tracer'у. Tracer может через `PTRACE_GETSIGINFO` прочитать `siginfo_t`, изменить его
через `PTRACE_SETSIGINFO`, подавить доставку (передав 0 в `data` для `PTRACE_CONT`) или подменить сигнал на другой. Это
ключевой механизм — именно так gdb перехватывает breakpoint'ы: после INT3 tracee получает SIGTRAP, gdb видит
signal-delivery-stop, откатывает rip на байт назад, восстанавливает оригинальный байт и продолжает.

**syscall-enter-stop / syscall-exit-stop**. Возникают только если tracer использует `PTRACE_SYSCALL` вместо
`PTRACE_CONT`. Kernel останавливает tracee на самой границе пользователь/ядро — на входе перед выполнением syscall и на
выходе перед возвратом в userspace. Между двумя stops syscall реально выполняется ядром, но tracer может его подменить (
изменив `orig_rax`) или подделать результат (изменив `rax` на выходе).

**PTRACE_EVENT_* stops**. Опциональные события: создание дочернего процесса/потока (FORK, VFORK, CLONE), вызов exec (
EXEC), завершение (EXIT), срабатывание seccomp-фильтра (SECCOMP), реакция на `PTRACE_INTERRUPT` (STOP). Tracer
подписывается на них через `PTRACE_SETOPTIONS`. Данные события (например, PID нового ребёнка) читаются через
`PTRACE_GETEVENTMSG`.

**single-step-stop**. После `PTRACE_SINGLESTEP` процессор устанавливает TF (Trap Flag) в EFLAGS, выполняет ровно одну
инструкцию и генерирует SIGTRAP. Tracer видит это как signal-delivery-stop с особым `si_code`.

**group-stop**. Job-control остановка, не связанная с ptrace. Возникает при доставке SIGSTOP/SIGTSTP/SIGTTIN/SIGTTOU. До
Linux 2.6.38 это путалось с ptrace-stop и приводило к гонкам; после введения `PTRACE_SEIZE` появилась чёткая семантика:
`PTRACE_LISTEN` позволяет дождаться выхода из group-stop, не возобновляя выполнение.

## Минимальный strace

Чтобы увидеть, как всё работает, достаточно ~50 строк C. Этот код запускает `/bin/ls`, перехватывает каждый syscall и
печатает его номер и возвращаемое значение.

```c
// minstrace.c — gcc -o minstrace minstrace.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "usage: %s prog args...\n", argv[0]); return 1; }

    pid_t pid = fork();
    if (pid == 0) {
        // tracee: разрешаем трассировку и зовём exec
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execvp(argv[1], &argv[1]);
        perror("execvp"); _exit(1);
    }

    // tracer: ждём первого stop после exec
    int status;
    waitpid(pid, &status, 0);
    // отличать syscall-stop от обычного SIGTRAP по биту 7
    ptrace(PTRACE_SETOPTIONS, pid, 0,
           PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL);

    int in_syscall = 0;
    long syscall_nr = 0;
    while (1) {
        if (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0) break;
        if (waitpid(pid, &status, 0) < 0) break;
        if (WIFEXITED(status)) break;

        // syscall-stop: WSTOPSIG == 0x80 | SIGTRAP
        if (WIFSTOPPED(status) && WSTOPSIG(status) == (SIGTRAP | 0x80)) {
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, pid, 0, &regs);
            if (!in_syscall) {
                syscall_nr = regs.orig_rax;
                fprintf(stderr, "syscall(%ld, %llx, %llx, %llx) = ",
                        syscall_nr,
                        (unsigned long long)regs.rdi,
                        (unsigned long long)regs.rsi,
                        (unsigned long long)regs.rdx);
                in_syscall = 1;
            } else {
                fprintf(stderr, "%lld\n", (long long)regs.rax);
                in_syscall = 0;
            }
        }
    }
    return 0;
}
```

Скомпилируйте и запустите: `gcc -o minstrace minstrace.c && ./minstrace /bin/ls`. Вы увидите весь поток syscall'ов от
запуска `ls`.

## Syscall tracing по тактам

Главное, что нужно понимать: `PTRACE_SYSCALL` даёт **два** stop'а на каждый syscall — на входе и на выходе. Это
позволяет не только видеть, что было вызвано, но и видеть, что вернулось.

```
такт │   tracee                     kernel                      tracer
─────┼───────────────────────────────────────────────────────────────────────────
  0  │  userspace код                                          ждёт в waitpid()
     │  ...
  1  │  mov rax, 0   (read)
     │  mov rdi, fd                                                ↑
     │  syscall  ─────────────────▶ переход в ring 0               │
  2  │                              kernel видит PT_PTRACED        │
     │                              ставит tracee в ptrace-stop ──▶ waitpid() возвращает
  3  │                                                              tracer:
     │                              ◀────────────────────────────── ptrace(GETREGS) → orig_rax=0
     │                                                              tracer:
     │                                                              решает: пропустить как есть
  4  │                              ◀────────────────────────────── ptrace(SYSCALL)
     │                              sys_read() реально работает
     │                              читает данные с диска/сокета
  5  │                              перед return to userspace
     │                              второй ptrace-stop ──────────▶ waitpid() возвращает
  6  │                                                              tracer:
     │                              ◀────────────────────────────── ptrace(GETREGS) → rax=N
     │                                                              tracer: лог "read = N"
  7  │                              ◀────────────────────────────── ptrace(SYSCALL)
     │  возврат в userspace                                        ↓
     │  rax = N                                                   ждёт следующего stop
     │  ...
```

На входе в syscall есть нюанс: `rax` в момент stop равен `-ENOSYS` (kernel ставит это как маркер «syscall ещё не
выполнен»), а реальный номер syscall — в `orig_rax`. На x86-64 аргументы передаются через `rdi`, `rsi`, `rdx`, `r10`,
`r8`, `r9` согласно SysV AMD64 ABI (в обычных функциях четвёртый аргумент — `rcx`, но `syscall` затирает `rcx` для
сохранения rip).

## Чтение и запись памяти tracee

`PTRACE_PEEKDATA` возвращает ровно одно машинное слово (на x86-64 — 8 байт). Чтобы прочитать строку — нужен цикл с
проверкой на NUL. Это медленно: каждый word — отдельный syscall.

Linux 3.2 добавил `process_vm_readv(2)` и `process_vm_writev(2)` — scatter-gather копирование памяти между двумя
процессами одним syscall'ом. Требует тех же прав, что и ptrace (CAP_SYS_PTRACE или соответствие Yama-политике), но не
требует attach и не требует stop. Это быстрейший способ.

Альтернатива — `/proc/PID/mem`: открыть, `lseek` на нужный адрес, `read`/`write`. Быстро для больших буферов, но
требует, чтобы tracee был остановлен (иначе данные могут меняться во время чтения).

| Способ                               | Скорость           | Stop нужен | Нужен attach | Размер за раз |
|--------------------------------------|--------------------|------------|--------------|---------------|
| `PTRACE_PEEKDATA` / `POKEDATA`       | медленно           | да         | да           | 1 word        |
| `/proc/PID/mem` read/write           | быстро для буферов | да         | нет¹         | любой         |
| `process_vm_readv` / `writev` (3.2+) | максимум           | нет²       | нет          | любой, iovec  |

¹ Технически открыть `/proc/PID/mem` без ptrace можно, но read/write требуют, чтобы целевой процесс был
ptrace-stop'нут (защита от race condition).
² Tracee может изменить буфер прямо в момент чтения — это race условие, ответственность вызывающего.

## Регистры

`PTRACE_GETREGS` копирует `struct user_regs_struct` в буфер tracer'а. На x86-64 структура содержит все GP-регистры (
rax..r15), rip, rflags, селекторы сегментов, `fs_base`/`gs_base` для TLS, и специальное поле `orig_rax` для syscall
tracking.

```c
struct user_regs_struct {
    unsigned long r15, r14, r13, r12;
    unsigned long rbp, rbx;
    unsigned long r11, r10, r9, r8;
    unsigned long rax, rcx, rdx, rsi, rdi;
    unsigned long orig_rax;          // оригинальный rax до syscall
    unsigned long rip, cs;
    unsigned long eflags;
    unsigned long rsp, ss;
    unsigned long fs_base, gs_base;
    unsigned long ds, es, fs, gs;
};
```

`PTRACE_GETREGSET` — более портабельная альтернатива: вместо фиксированной структуры передаётся `struct iovec` и тип
набора через `addr` (`NT_PRSTATUS` для GP-регистров, `NT_PRFPREG` для FPU, `NT_X86_XSTATE` для полного XSAVE-area c
AVX/AVX-512). На ARM/RISC-V `GETREGS` либо вовсе отсутствует, либо имеет другую структуру; `GETREGSET` работает везде.

Изменить регистры — `SETREGS`/`SETREGSET`. Самое мощное применение: переписать `rip` и продолжить — tracee выполнится из
совершенно другого места. Так работает `gdb`'шная команда `jump`, а с подменой нескольких регистров — и
`call function()`.

## Дочерние процессы и потоки

По умолчанию ptrace следит только за тем процессом, к которому был сделан attach. Если tracee делает `fork()` или
`clone()` — потомок выходит из-под наблюдения. Чтобы автоматически захватывать всё дерево, нужны опции:

| Опция                   | Эффект                                                                 |
|-------------------------|------------------------------------------------------------------------|
| `PTRACE_O_TRACEFORK`    | stop при fork; новый PID → `GETEVENTMSG`; child автоматически attached |
| `PTRACE_O_TRACEVFORK`   | то же для vfork                                                        |
| `PTRACE_O_TRACECLONE`   | то же для clone (включая pthread_create)                               |
| `PTRACE_O_TRACEEXEC`    | stop при exec, можно проверить новый бинарь                            |
| `PTRACE_O_TRACEEXIT`    | stop перед самим exit, регистры ещё валидны                            |
| `PTRACE_O_TRACESYSGOOD` | syscall-stop приходит как `SIGTRAP \| 0x80` — отличить от обычного     |
| `PTRACE_O_TRACESECCOMP` | stop по `SECCOMP_RET_TRACE` (комбинация с seccomp-фильтром)            |
| `PTRACE_O_EXITKILL`     | если tracer умирает — kill всех tracee; защита от orphan'ов            |

`TRACESYSGOOD` — практически обязательная опция. Без неё syscall-stop приходит как обычный SIGTRAP, и tracer не может
отличить его от breakpoint'а или single-step. С опцией бит 7 SIGTRAP'а взводится, и `WSTOPSIG == 0x85`.

Multi-threaded tracing — отдельная боль. Каждый поток (tracee thread) ptrace рассматривает как независимую сущность;
attach к одному потоку не attach'ит остальные. `PTRACE_O_TRACECLONE` помогает, но gdb всё равно вынужден работать с
массивом TID'ов и синхронизировать group-stop вручную — отсюда `set scheduler-locking on/off` и сложности с пошаговой
отладкой многопоточного кода.

## PTRACE_SEIZE: почему ATTACH плох

`PTRACE_ATTACH` имеет неприятную особенность: он отправляет tracee SIGSTOP. Это означает, что tracee оказывается
одновременно в ptrace-stop и в group-stop — два разных состояния со своей логикой. Tracer не может сразу делать
ptrace-операции; ему нужно сначала дождаться доставки SIGSTOP через wait, потом подавить его, и только потом начинать
работу. Гонки между group-stop и signal-delivery-stop приводят к багам, которые ловились в strace и gdb годами.

`PTRACE_SEIZE` (Linux 3.4+) решает это радикально:

- Не отправляет SIGSTOP — tracee продолжает работать как ни в чём не бывало
- Tracer может включить опции (`PTRACE_SETOPTIONS`) прямо в момент seize через `data`
- Для остановки используется `PTRACE_INTERRUPT` — явный, отдельный механизм без сигналов
- `PTRACE_LISTEN` позволяет дождаться выхода tracee из group-stop без возобновления выполнения

Modern tooling — strace ≥ 4.9, gdb ≥ 7.4 — использует SEIZE по умолчанию. ATTACH остаётся ради совместимости со старыми
ядрами.

## Code injection через ptrace

Если tracer может писать в память и менять rip, он может выполнить произвольный код от имени tracee. Эта техника лежит в
основе DLL-инжекторов, CRIU parasite code и команды `call` в gdb. Алгоритм:

```
   ┌────────────────────────────────────────────────────────────────────────┐
   │                       code injection flow                              │
   └────────────────────────────────────────────────────────────────────────┘

  ┌─────────┐                                              ┌─────────────┐
  │ tracer  │                                              │   tracee    │
  └────┬────┘                                              └──────┬──────┘
       │                                                          │
       │  1. PTRACE_ATTACH (или SEIZE + INTERRUPT)                │
       │ ────────────────────────────────────────────────────────▶│
       │                                            ptrace-stop ◀─│
       │                                                          │
       │  2. PTRACE_GETREGS — сохранить rip, rsp, rax, ...        │
       │                                                          │
       │  3. PTRACE_PEEKDATA(rip) — сохранить старые байты        │
       │  ┌─────────────────────────────┐                         │
       │  │ save: 48 89 e5 ... (8 байт) │                         │
       │  └─────────────────────────────┘                         │
       │                                                          │
       │  4. POKETEXT(rip, syscall_stub)                          │
       │     stub: mov rax,9 / mov rdi,0 / ... / syscall          │
       │     (вызов mmap для выделения страницы под shellcode)    │
       │                                                          │
       │  5. PTRACE_CONT, ждать SIGTRAP/SIGSEGV после stub        │
       │ ────────────────────────────────────────────────────────▶│
       │                                          выполняет stub  │
       │                                          mmap → rax=addr │
       │                                                   trap  ◀│
       │                                                          │
       │  6. GETREGS → rax = адрес новой страницы                 │
       │                                                          │
       │  7. POKETEXT(rax_новая_страница, shellcode)              │
       │     (или process_vm_writev для скорости)                 │
       │                                                          │
       │  8. SETREGS: rip = rax_новая_страница                    │
       │     PTRACE_CONT                                          │
       │ ────────────────────────────────────────────────────────▶│
       │                                       выполняет shellcode│
       │                                       (открывает sock,   │
       │                                        грузит .so, ...)  │
       │                                                          │
       │  9. После завершения shellcode (int3 в конце):           │
       │     SIGTRAP                                              │
       │ ◀──────────────────────────────────────────────────────  │
       │                                                          │
       │ 10. POKETEXT — восстановить байты по старому rip         │
       │     SETREGS — восстановить регистры                      │
       │     PTRACE_DETACH                                        │
       │ ────────────────────────────────────────────────────────▶│
       │                                  продолжает обычную жизнь│
       │                                  как ни в чём не бывало  │
       ▼                                                          ▼
```

Тонкости. `mmap` нужен потому, что обычно нет готового исполняемого региона, куда можно безопасно писать произвольный
код (а если есть — например `.text` существующей библиотеки — это всё равно безопаснее не трогать). Stub должен
заканчиваться `int3` (опкод 0xCC) — это безусловный SIGTRAP, по которому tracer узнаёт, что stub отработал. Реальные
инжекторы дополнительно сохраняют сигналы (через `PTRACE_GETSIGMASK` Linux 3.11+), чтобы не потерять in-flight доставку.

Применения за пределами эксплоитов: `gdb call function()` ровно это и делает (только без `mmap` — gdb пишет stub поверх
`_start` или другой невостребованной области и аккуратно восстанавливает). CRIU использует «parasite code» — `.so` с
заранее известными адресами, который инжектируется во все процессы перед dump'ом, чтобы из него же читать состояние
памяти, открытых fd, mmap-регионов. libinjector делает то же для DLL-инжекции в Windows-эмулятор Wine.

## PTRACE_O_TRACESECCOMP

seccomp-фильтр может возвращать `SECCOMP_RET_TRACE` — это означает «не убивать, не пропускать, а позвать tracer'а». В
сочетании с `PTRACE_O_TRACESECCOMP` это даёт мощную модель:

- seccomp-bpf фильтрует **быстро** в kernel'е — пропускает 99% syscall'ов без оверхеда
- Интересные syscall'ы попадают в ptrace-stop, и tracer решает: разрешить, подменить, заблокировать

Это основа `gVisor` (Google) — userspace-гипервизора, который перехватывает все syscall'ы гостевой ОС и эмулирует их в
userspace «гофере», но через seccomp+ptrace фильтрует трафик так, чтобы дорогие ptrace-stops случались только когда
действительно нужно. Похожим образом устроен Firejail для нестандартных filter actions, которые не выразимы в чистом
seccomp-bpf.

## Безопасность: Yama LSM

В классической модели Unix tracer мог attach'иться к любому процессу того же UID. Это удобно для разработчика и
катастрофично для безопасности: если злоумышленник получил RCE в любом процессе пользователя, он может attach'иться к
запущенному `ssh-agent` или браузеру и украсть ключи/cookies.

Linux 3.4 ввёл LSM-модуль **Yama**, ограничивающий ptrace. Управляется через `/proc/sys/kernel/yama/ptrace_scope`:

| Значение | Семантика                                                                 |
|----------|---------------------------------------------------------------------------|
| `0`      | classic Unix: любой процесс того же UID может attach                      |
| `1`      | restricted (default Ubuntu/Debian/Fedora): только родитель в цепочке fork |
| `2`      | admin-only: нужен `CAP_SYS_PTRACE`                                        |
| `3`      | disabled: ptrace полностью выключен, требует перезагрузки чтобы вернуть   |

При значении 1 не-родитель всё ещё может attach, если tracee явно разрешил это через
`prctl(PR_SET_PTRACER, tracer_pid)` — этим пользуется GNOME для crash reporter'а.

Дополнительные ограничения, всегда активные:

- ptrace не работает для setuid/setgid бинарей — kernel сбрасывает effective UID при ptrace-stop, либо отказывает в
  attach (зависит от того, в какой момент попытались)
- Один tracer на одного tracee: нельзя attach'нуть две gdb-сессии к одному процессу
- Внутри user namespace capabilities считаются относительно ns; root в ns может trace-ить процессы только из своего ns

## Производительность

Цена ptrace-наблюдения видна в простом расчёте. Каждый syscall с `PTRACE_SYSCALL` даёт **2 stop'а** (enter + exit).
Каждый stop — это:

- Переход kernel → kernel: tracee останавливается, ставится в очередь ожидания
- Переход kernel → tracer: wakeup tracer'а, его `waitpid()` возвращает
- tracer делает несколько ptrace-вызовов (GETREGS, возможно PEEKDATA для строк) — каждый syscall сам по себе
- Tracer делает `ptrace(PTRACE_SYSCALL)` — kernel размораживает tracee
- Context switch tracer → tracee

Суммарно: **10–20 context switch'ей на каждый syscall**. На современном x86 context switch стоит ~1–3 µs; ptrace
добавляет ~20–50 µs к каждому наблюдаемому syscall. Для CPU-heavy workload это незаметно, для syscall-heavy (сетевой
сервер, компилятор, веб-краулер) замедление 10–100× — норма, а 1000× — не редкость.

Поэтому современная observability ушла с ptrace на kernel-side трассировщики:

| Инструмент        | Где работает | Overhead syscall tracing | Может менять syscall |
|-------------------|--------------|--------------------------|----------------------|
| strace (ptrace)   | userspace    | 10×–1000×                | да                   |
| `perf trace`      | kernel-side  | 1.5×–3×                  | нет                  |
| `bpftrace` (eBPF) | kernel-side  | 1.05×–1.2×               | нет¹                 |
| `ftrace`          | kernel       | минимальный              | нет                  |
| `uprobes` / USDT  | kernel+user  | средний                  | нет                  |

¹ eBPF может *модифицировать* поведение через `bpf_override_return`, но это требует CAP_BPF и ограничено.

ptrace остаётся незаменимым там, где нужно именно **синхронное вмешательство**: остановиться на breakpoint, изменить
аргумент connect перед выполнением, инжектировать код. Для пассивного наблюдения — eBPF.

## Где используется ptrace

| Инструмент     | Как использует ptrace                                                                             |
|----------------|---------------------------------------------------------------------------------------------------|
| `gdb`, `lldb`  | breakpoints через POKETEXT INT3; SINGLESTEP; GETREGS для backtrace; watchpoints через DR-регистры |
| `strace`       | PTRACE_SYSCALL + читает регистры и память, декодирует аргументы по таблице                        |
| `ltrace`       | PTRACE_SYSCALL + breakpoints на PLT для перехвата вызовов библиотечных функций                    |
| `CRIU`         | seize всех процессов, инжекция parasite code для dump памяти/fd/mmap'ов                           |
| `Firejail`     | seccomp-bpf + TRACESECCOMP для kill/log/modify нестандартных syscall'ов                           |
| `bubblewrap`   | то же                                                                                             |
| `gVisor`       | seize гостевых процессов, перехват каждого syscall, эмуляция в userspace gofer                    |
| `libinjector`  | shellcode injection в чужой процесс через классический POKETEXT+SETREGS flow                      |
| `rr` (Mozilla) | record & replay debugger: ptrace + seccomp для детерминированной перезаписи                       |
| `graftcp`      | прозрачное проксирование `connect()` через подмену sockaddr в syscall-enter-stop                  |

## graftcp: прозрачное проксирование через ptrace

[graftcp](https://github.com/hmgle/graftcp) — пример нестандартного использования ptrace, выходящего за рамки
«debugger/tracer». Задача: запустить произвольную программу так, чтобы её TCP-соединения шли через SOCKS5/HTTP-прокси,
не требуя:

- root-прав (как iptables REDIRECT)
- LD_PRELOAD (не работает со static binaries и обходится через прямой `syscall(2)`)
- поддержки прокси самим приложением

### Архитектура

graftcp состоит из двух компонентов:

```
       ┌──────────────────────────────────────────────────────────────┐
       │                       graftcp архитектура                    │
       └──────────────────────────────────────────────────────────────┘

  пользователь:  $ graftcp -- curl https://api.example.com

   ┌──────────────────┐
   │  graftcp (C)     │  tracer, запускает target через fork+exec+TRACEME
   │                  │  перехватывает connect/clone/close
   └────┬─────────────┘
        │ ptrace
        ▼
   ┌──────────────────┐
   │  curl (tracee)   │  ничего не знает о прокси
   │                  │  думает что соединяется напрямую
   └────┬─────────────┘
        │ TCP к 1.2.3.4:443?
        │ → подменено на 127.0.0.1:2233
        ▼
   ┌──────────────────┐         ┌──────────────────┐
   │ graftcp-local    │──SOCKS─▶│  upstream proxy  │
   │ (Go)             │         │  127.0.0.1:1080  │
   │ слушает          │         └──────────────────┘
   │ 127.0.0.1:2233   │
   └──────────────────┘
```

- **graftcp** — tracer на C, запускает целевую программу и ptrace'ит её
- **graftcp-local** — Go-демон, слушает на localhost:2233, говорит SOCKS5/HTTP с настоящим upstream-proxy

Связь между ними — через Unix socket. Когда tracer перехватывает `connect`, он сообщает graftcp-local: «соединение на fd
N от PID P должно идти на 1.2.3.4:443». graftcp-local запоминает это и, когда tracee подключится к 127.0.0.1:2233,
поднимет SOCKS5-туннель в правильное место.

### Перехват connect

Главный трюк — подмена `sockaddr` в памяти tracee прямо в syscall-enter-stop:

```
                ┌──────────────────────────────────────────────────────┐
                │           graftcp: перехват connect()                │
                └──────────────────────────────────────────────────────┘

tracee                                tracer                graftcp-local
  │                                     │                        │
  │  fd = socket(AF_INET, ...)          │                        │
  │  sockaddr.sin_addr = 1.2.3.4        │                        │
  │  sockaddr.sin_port = 443            │                        │
  │  connect(fd, &sockaddr, 16)         │                        │
  │  syscall ──────────────────▶ ENTER stop                      │
  │                                     │                        │
  │                                     │  GETREGS:              │
  │                                     │    orig_rax = 42 (connect)
  │                                     │    rdi = fd            │
  │                                     │    rsi = &sockaddr     │
  │                                     │    rdx = 16            │
  │                                     │                        │
  │                                     │  process_vm_readv:     │
  │                                     │    sockaddr из памяти  │
  │                                     │                        │
  │                                     │  фильтр: AF_INET? да   │
  │                                     │                        │
  │                                     │  сохранить в таблице:  │
  │                                     │    {pid,fd} → 1.2.3.4:443
  │                                     │                        │
  │                                     │  через Unix socket: ──▶│
  │                                     │  "pid=P fd=N           │ зарегистрировал
  │                                     │   target=1.2.3.4:443"  │ ожидающее соединение
  │                                     │                        │
  │                                     │  POKETEXT/writev:      │
  │                                     │  подменить sockaddr →  │
  │                                     │  127.0.0.1:2233        │
  │                                     │                        │
  │                                     │  PTRACE_SYSCALL        │
  │  ◀───────────────────────────       │                        │
  │  kernel выполняет connect на        │                        │
  │  127.0.0.1:2233                     │                        │
  │                                     │                        │
  │                                     │ ◀── accept() ──────────│
  │                                     │                        │ ─── socks5 handshake ──▶
  │                                     │                        │ ─── connect 1.2.3.4 ───▶
  │                                     │                        │ ◀── proxy connected ───
  │                                     │                        │
  │  возврат: rax = 0                   │                        │
  │  ◀──────────────────────  EXIT stop                          │
  │                                     │  (опционально          │
  │                                     │   восстановить байты)  │
  │                                     │  PTRACE_SYSCALL        │
  │  ◀───────────────────────────       │                        │
  │                                     │                        │
  │  read/write на fd → идут через      │                        │
  │  graftcp-local → upstream proxy →   │                        │
  │  настоящий destination              │                        │
```

Перехватываемые syscall'ы:

- **`connect`** — главный, описан выше
- **`clone`** — иначе forked children не наследуют ptrace; graftcp должен явно подхватывать новые потоки и процессы
  через `PTRACE_O_TRACECLONE` / `TRACEFORK`
- **`close`** — для очистки таблицы соответствий `{pid,fd} → original_dest`

### Тонкости

**Подмена sockaddr in-place**. Buffer длиной до 28 байт (sockaddr_in6) пишется обратно через `POKETEXT` пословно или
одним вызовом `process_vm_writev`. `addrlen` (rdx) обычно подменять не нужно, так как 127.0.0.1:port укладывается в
любой `sockaddr_in*`.

**IPv6**. `sockaddr_in6` длиннее (28 байт), формат другой. graftcp проверяет `sa_family` и обрабатывает оба случая. Если
IPv6-цель не поддерживается upstream-прокси — соединение можно либо отклонить, либо завернуть через IPv4-loopback (как
делает graftcp).

**TCP only**. UDP перехватывать практически бесполезно: SOCKS5 UDP-ассоциация — отдельный механизм с собственным
форматом пакетов, и подменой `sockaddr` в `sendto` дело не ограничивается — пришлось бы инкапсулировать каждый пакет.
graftcp осознанно UDP не трогает.

**Cleanup**. После EXIT-stop tracer может восстановить оригинальные байты sockaddr (вдруг tracee ещё раз использует тот
же буфер?). На практике это редко критично — большинство приложений выделяют sockaddr на стеке и забывают про него.

### Сравнение с альтернативами

| Подход                                | Static binaries | Root   | Влияет на весь хост | Лимит                      |
|---------------------------------------|-----------------|--------|---------------------|----------------------------|
| `proxychains` (LD_PRELOAD)            | нет             | нет    | нет                 | обходится прямым `syscall` |
| iptables REDIRECT + transparent SOCKS | да              | **да** | **да**              | сложно настроить           |
| `redsocks` + iptables                 | да              | **да** | **да**              | то же                      |
| `graftcp`                             | **да**          | нет    | нет                 | overhead ptrace            |

graftcp выигрывает в нише «обычный пользователь хочет завернуть один процесс через прокси без правки системы». Цена —
ptrace-overhead на каждое соединение: для long-lived connection незаметно, для краулера с тысячами коротких
HTTP-запросов — десятки процентов CPU.

## Альтернативы ptrace

Для каждой задачи, исторически решавшейся через ptrace, сейчас есть более лёгкая альтернатива:

| Задача                     | ptrace                   | Современная альтернатива                      |
|----------------------------|--------------------------|-----------------------------------------------|
| debugger                   | gdb через ptrace         | `gdbserver` (тоже ptrace, но удалённый)       |
| syscall tracing            | strace                   | `bpftrace`, `perf trace`, audit framework     |
| userspace function tracing | ltrace (PLT breakpoints) | `uprobes`, USDT, eBPF `uprobe`                |
| sandbox enforcement        | seccomp + ptrace         | чистый seccomp-bpf (без ptrace)               |
| code injection             | POKETEXT + SETREGS       | `LD_PRELOAD` (если не static), kernel-модули  |
| process snapshotting       | CRIU + parasite          | альтернатив пока нет, ptrace вне конкуренции  |
| transparent connect proxy  | graftcp                  | iptables (с root); LD_PRELOAD (не для static) |

## Связанные темы

- [Отладка (gdb, strace, sanitizers, perf)](debugging.md) — основные инструменты на ptrace
- [seccomp](../isolation/seccomp.md) — комбинируется с ptrace через `SECCOMP_RET_TRACE`
- [Сигналы](../processes/signals.md) — ptrace-stops это в основном сигналы (SIGSTOP, SIGTRAP, синтетические)
- [Системные вызовы](../assembler_and_processor/system_calls_introduction.md) — что именно перехватывает
  `PTRACE_SYSCALL`
- [Context switch](../processes/context_switch.md) — каждый ptrace-stop это набор переключений контекста

## Источники

- `man 2 ptrace`, `man 2 waitpid`, `man 2 process_vm_readv`
- Linux kernel: `kernel/ptrace.c`, `arch/x86/kernel/ptrace.c`
- [Yama LSM documentation](https://www.kernel.org/doc/html/latest/admin-guide/LSM/Yama.html)
- [strace source](https://github.com/strace/strace) — `src/strace.c`, `src/syscall.c`
- [graftcp](https://github.com/hmgle/graftcp) — sources и README
- [CRIU parasite code](https://criu.org/Parasite_code) — как делается инжекция
- Eli
  Bendersky, [How debuggers work, part 3](https://eli.thegreenplace.net/2011/01/27/how-debuggers-work-part-3-debugging-information)
- [PTRACE_SEIZE: clean ptrace attach](https://lwn.net/Articles/446593/) — LWN, история появления SEIZE
