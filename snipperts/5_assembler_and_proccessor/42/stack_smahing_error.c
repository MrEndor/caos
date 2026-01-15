#include <stdio.h>
#include <string.h>

void vulnerable(const char *input) {
    char buffer[8];
    strcpy(buffer, input);
}

int main() {
    vulnerable("AAAAAAAAAAAAAAAA");
}
