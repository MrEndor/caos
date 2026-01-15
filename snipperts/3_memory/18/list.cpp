#include <list>

int main() {
    std::list<int> lst(100'000);
    for (auto& elem : lst) {
        ++elem;
    }
}

