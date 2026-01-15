void trigger_segfault() {
    asm volatile("mov $0, %%rax\n\t"
                 "mov (%%rax), %%rbx"
                 : : : "rax", "rbx");
}
