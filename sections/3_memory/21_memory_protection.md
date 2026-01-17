##### Какие бывают права доступа к памяти?

Для каждой страницы в таблице страниц есть биты прав:

- `R` (PROT_READ) — чтение;
- `W` (PROT_WRITE) — запись;
- `X` (PROT_EXEC) — исполнение кода.

Комбинации прав задаются в `prot` при `mmap` и могут изменяться через `mprotect`.

##### Зачем нужен сисколл mprotect и как им пользоваться?

```c
#include <sys/mman.h>

int mprotect(void *addr, size_t len, int prot);
```

Изменяет права доступа к диапазону страниц, начинающемуся с `addr` и длиной `len` (округляется до границ страниц).

Пример: сделать область только для чтения:

```c
mprotect(ptr, size, PROT_READ);
```

Если потом попытаться записывать по этому адресу, будет `SIGSEGV`.

##### Покажите, как с помощью mmap и mprotect загрузить код из библиотеки в память на выполнение.

1. Открыть файл с машинным кодом (например, маленькая библиотека с одной функцией).
2. `mmap` его с правами `PROT_READ | PROT_EXEC` и флагом `MAP_PRIVATE`.
3. Привести указатель к функции подходящего типа и вызвать.

Упрощённый набросок (без проверки ABI/смещений):

libmath.c
```c
double squre(double value) {
    return value * value;
}
```

```c
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char **argv) {
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    fstat(fd, &st);

    void *addr = mmap(NULL, st.st_size,
                      PROT_READ | PROT_EXEC,
                      MAP_PRIVATE,
                      fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    double (*func)(double) = (double (*)(double))(addr + 0x40);
    double res = func(2.0);
    printf("%f\n", res);

    munmap(addr, st.st_size);
    close(fd);
}
```

На практике для динамической загрузки библиотек используют `dlopen`/`dlsym`, а не такой «сырой» способ.

##### Что означает ошибка Illegal intstruction? Покажите пример программы на Си, которая приводит к этой ошибке (естественным путем, т.е. не генерируя эту ошибку из кода напрямую).

Сигнал `SIGILL` (`Illegal instruction`) процесс получает, когда процессор пытается выполнить
**некорректную инструкцию**:

- данные в памяти не являются валидным машинным кодом;
- используется инструкция, не поддерживаемая данным CPU (например, AVX‑512 на старом процессоре без поддержки);
- переход попал «в середину» инструкции из‑за повреждения стека/кода.

Простейший способ воспроизвести `Illegal instruction` — сгенерировать в памяти байт, который интерпретатор кода считает
инструкцией `INT3`/`UD2` или просто мусором, и передать управление туда:

```c
#include <sys/mman.h>
#include <string.h>

int main() {
    void *ptr = mmap(NULL, 4096,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);

    // Записать 0xCC (INT3) или последовательность, не являющуюся валидным кодом
    memset(ptr, 0xFF, 4096);

    void (*f)() = (void (*)())ptr;
    f();    // при попытке выполнения получим SIGILL/SIGSEGV
}
```

(На реальных системах часто действует политика W^X и DEP, поэтому сочетать PROT_WRITE и PROT_EXEC не рекомендуется; это
всего лишь учебный пример:)
