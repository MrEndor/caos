#include <fcntl.h>
#include <unistd.h>

int main() {
    int tty = open("/dev/tty", O_WRONLY);
    write(tty, "Hello\n", 6);
    close(tty);
}
