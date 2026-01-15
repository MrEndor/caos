#include <thread>

class ThreadGuard {
    std::thread& t;
public:
    explicit ThreadGuard(std::thread& t_) : t(t_) {}
    ~ThreadGuard() {
        if (t.joinable()) {
            t.join();
        }
    }
};

void worker() {}

int main() {
    std::thread t(worker);
    ThreadGuard guard(t);
    // ...
    return 0;
}
