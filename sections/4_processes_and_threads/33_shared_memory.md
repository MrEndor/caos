##### Что такое разделяемая память?

**Shared memory** -- это механизм IPC, при котором два или более процесса имеют доступ к одному и тому же участку
физической памяти. Это позволяет быстро обмениваться большими объёмами данных без копирования.

Особенности:

- **быстро** (обращение к памяти, без системных вызовов при каждой операции);
- **большие объёмы** (можно разделить несколько MB);
- **требует синхронизации** (нужны мьютексы, семафоры и т.д. для избежания race conditions);
- **явное управление** (создание, присоединение, отсоединение, удаление).

##### Какие сисколлы существуют для создания и управления разделяемой памятью?

**POSIX API:**

- `shm_open()` -- создать/открыть segmen разделяемой памяти;
- `mmap()` -- смапить в адресное пространство;
- `shm_unlink()` -- удалить.

**System V API (старый):**

- `shmget()` -- создать или получить сегмент;
- `shmat()` -- присоединить к адресному пространству;
- `shmdt()` -- отсоединить;
- `shmctl()` -- управление (удаление и т.д.).

POSIX-версия проще и современнее, обычно её рекомендуют.

##### Покажите на примере, как устроить общение через разделяемую память между двумя процессами.

**Процесс 1 (писатель):**

```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *shm_name = "/my_shared_mem";
    size_t size = 1024;
    
    // Создать/открыть разделяемую память
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }
    
    // Установить размер
    if (ftruncate(fd, size) == -1) {
        perror("ftruncate");
        return 1;
    }
    
    // Смапить в память
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    // Написать данные
    const char *message = "Hello from shared memory!";
    strcpy((char *)addr, message);
    
    printf("Written: %s\n", (char *)addr);
    
    // Очистить
    munmap(addr, size);
    close(fd);
    // Не удаляем shm_name, пусть читатель его читает
    
    sleep(5);  // дать время читателю
    
    shm_unlink(shm_name);
    return 0;
}
```

**Процесс 2 (читатель):**

```c
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    const char *shm_name = "/my_shared_mem";
    size_t size = 1024;
    
    sleep(1);  // дать время писателю создать
    
    // Открыть существующую разделяемую память
    int fd = shm_open(shm_name, O_RDONLY, 0666);
    if (fd == -1) {
        perror("shm_open");
        return 1;
    }
    
    // Смапить в память
    void *addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    // Прочитать данные
    printf("Read: %s\n", (char *)addr);
    
    // Очистить
    munmap(addr, size);
    close(fd);
}
```

**Компилирование и запуск:**

```bash
gcc -o writer writer.c -lrt
gcc -o reader reader.c -lrt

./writer &
./reader
# Output:
# Written: Hello from shared memory!
# Read: Hello from shared memory!
```

##### Как посмотреть, какие участки разделяемой памяти существуют в ОС и кто их создал?

```bash
# POSIX API
ls -la /dev/shm/           # обычно здесь хранятся shm_open() сегменты

# System V API
ipcs -m                    # показать все System V shared memory
ipcs -m -p                 # с показом PID создателя и последнего юзера
```

##### Как посмотреть, какие страницы разделяемой памяти сейчас использует данный процесс?

```bash
cat /proc/<pid>/maps       # показать все выделенные память процесса
pmap <pid>                 # красиво отформатированный вывод

# Специфично для shared memory:
ipcs -m
# Результат содержит инфу о подключённых процессах
```

Пример вывода `pmap`:

```
ADDRESS             PERM  SIZE DEVICE  OFFSET  OBJECT
0000564e3b23f000   rw-p   20K [heap]
0000564e3b249000   r--p   52K /lib64/libc.so.6
/dev/shm/my_shm    rw-s 1024K [shared memory]
```
