#include <unistd.h>

int main() {
    return 0;
}

extern "C" void my_start() {
    int code = main();
    _exit(code);
}