#include <iostream>

int global_var = 42;
static int static_var = 10;

void regular_func() { std::cout << "Regular\n"; }
static void static_func() { std::cout << "Static\n"; }

__attribute__((weak)) void weak_func() { std::cout << "Weak\n"; }
__attribute__((visibility("hidden"))) void hidden_func() {
    std::cout << "Hidden\n";
}

namespace MyNS {
    class MyClass {
    public:
        void method(int x, double y) {
            std::cout << x << " " << y << "\n";
        }
    };
}

int main() {
    MyNS::MyClass obj;
    obj.method(5, 3.14);
    regular_func();
    static_func();
    weak_func();
    hidden_func();
    return 0;
}
