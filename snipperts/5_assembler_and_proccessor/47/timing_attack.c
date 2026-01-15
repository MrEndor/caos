#include <stdint.h>

int insecure_string_compare(const char *input, const char *stored) {
    for (int i = 0; i < strlen(stored); i++) {
        if (input[i] != stored[i])
            return 0;  // выход при первом несовпадении (ОПАСНО!)
    }
    return 1;
}

int constant_time_string_compare(const uint8_t *a, const uint8_t *b, int len) {
    volatile uint8_t result = 0;

    // Проверить ВСЕ байты, даже если нашли несовпадение
    for (int i = 0; i < len; i++) {
        result |= a[i] ^ b[i];  // XOR даёт 0 если совпадает
    }

    return result == 0;
}

int constant_time_string_compare(const uint8_t *a, const uint8_t *b, int len) {
    volatile uint8_t result = 0;

    // Запретить оптимизации и использовать volatile memory operands
    asm volatile(
        "xorl %%eax, %%eax\n\t"
        "1: cmpb (%1), (%2)\n\t"
        "   orl %%eax, %0\n\t"   // accumulate XOR result в memory
        "   incq %1\n\t"
        "   incq %2\n\t"
        "   decl %3\n\t"
        "   jnz 1b"
        : "+m"(result)
        : "r"(a), "r"(b), "r"(len)
        : "rax"
    );

    return result == 0;
}
