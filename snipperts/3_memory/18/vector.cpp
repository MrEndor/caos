#include <vector>

int main() {
    std::vector<int> vec(100'000);
    for (int i = 0; i < 100'000; ++i) {
        ++vec[i];
    }
}
