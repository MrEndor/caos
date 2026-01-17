#include <sys/mman.h>
#include <string.h>

int main() {
    void *ptr = mmap(NULL, 4096,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);

    memset(ptr, 0xFF, 4096);

    void (*f)() = (void (*)())ptr;
    f();
}
