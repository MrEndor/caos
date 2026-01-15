void disable_interrupts() {
    asm volatile("cli");  // отключить прерывания
    // General Protection Fault (#GP) → SIGSEGV
}

int main() {
    disable_interrupts();
}
