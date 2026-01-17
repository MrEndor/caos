#include <dlfcn.h>
#include <iostream>

int main() {
    void* lib = dlopen("./libmath.so", RTLD_LAZY);
    if (!lib) {
        std::cerr << dlerror() << std::endl;
        return 1;
    }

    auto sqr = (double(*)(double))dlsym(lib, "square");
    if (!sqr) {
        std::cerr << dlerror() << std::endl;
        return 1;
    }

    std::cout << sqr(2.0) << std::endl;
    dlclose(lib);
}
