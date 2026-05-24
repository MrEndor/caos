// vdso_benchmark.cpp
#define _GNU_SOURCE
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <sched.h>
#include <cstring>
#include <iomanip>

constexpr size_t N_WARMUP = 100000;
constexpr size_t N_ITERATIONS = 10000000;

class VDSOBenchmark {
private:
    void print_maps() {
        std::cout << "\n=== Карта памяти (проверка vDSO) ===" << std::endl;
        FILE* maps = fopen("/proc/self/maps", "r");
        if (!maps) return;

        char line[256];
        while (fgets(line, sizeof(line), maps)) {
            if (strstr(line, "[vdso]")) {
                std::cout << "✓ vDSO найдена: " << line;
                break;
            }
        }
        fclose(maps);
    }

public:
    void run() {
        print_maps();
        std::cout << "\n=== Бенчмарк vDSO vs Syscall (x86-64) ===" << std::endl;
        std::cout << std::string(60, '=') << std::endl;

        benchmark_clock_gettime();
        print_summary();
    }

private:
    void benchmark_getpid() {
        pid_t pid;
        for (size_t i = 0; i < N_WARMUP; ++i) pid = getpid();

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N_ITERATIONS; ++i) {
            pid = getpid();  // vDSO
        }
        auto end = std::chrono::high_resolution_clock::now();

        double ns_per_call = std::chrono::duration<double, std::nano>(end - start).count() / N_ITERATIONS;
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "getpid() (vDSO):      " << ns_per_call << " нс/вызов" << std::endl;
        getpid_times[0] = ns_per_call;
    }

    void benchmark_gettimeofday() {
        struct timeval tv;
        for (size_t i = 0; i < N_WARMUP; ++i) gettimeofday(&tv, nullptr);

        auto start = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N_ITERATIONS; ++i) {
            gettimeofday(&tv, nullptr);  // vDSO
        }
        auto end = std::chrono::high_resolution_clock::now();

        double ns_per_call = std::chrono::duration<double, std::nano>(end - start).count() / N_ITERATIONS;
        std::cout << "gettimeofday() (vDSO): " << ns_per_call << " нс/вызов" << std::endl;
        gettimeofday_times[0] = ns_per_call;
    }

    void benchmark_clock_gettime() {
        struct timespec ts;

        // vDSO
        for (size_t i = 0; i < N_WARMUP; ++i) {
            clock_gettime(CLOCK_MONOTONIC, &ts);
        }
        auto start_vdso = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N_ITERATIONS; ++i) {
            clock_gettime(CLOCK_MONOTONIC, &ts);  // vDSO
        }
        auto end_vdso = std::chrono::high_resolution_clock::now();

        double vdso_ns = std::chrono::duration<double, std::nano>(end_vdso - start_vdso).count() / N_ITERATIONS;

        // Syscall для сравнения
        for (size_t i = 0; i < N_WARMUP; ++i) {
            syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
        }
        auto start_syscall = std::chrono::high_resolution_clock::now();
        for (size_t i = 0; i < N_ITERATIONS; ++i) {
            syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
        }
        auto end_syscall = std::chrono::high_resolution_clock::now();

        double syscall_ns = std::chrono::duration<double, std::nano>(end_syscall - start_syscall).count() / N_ITERATIONS;

        std::cout << "clock_gettime() (vDSO): " << vdso_ns << " нс/вызов" << std::endl;
        std::cout << "clock_gettime() (syscall): " << syscall_ns << " нс/вызов"
                  << " (" << (syscall_ns/vdso_ns) << "x медленнее)" << std::endl;

        clock_gettime_times[0] = vdso_ns;
        clock_gettime_times[1] = syscall_ns;
    }

    void print_summary() {
        std::cout << "\n=== ИТОГОВАЯ ТАБЛИЦА ===" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << std::left << std::setw(25) << "Функция"
                  << std::setw(12) << "vDSO"
                  << std::setw(12) << "Syscall"
                  << "Ускорение" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        std::cout << std::left << std::setw(25) << "clock_gettime()"
                  << std::setw(10) << clock_gettime_times[0] << " нс"
                  << std::setw(10) << clock_gettime_times[1] << " нс"
                  << std::fixed << std::setprecision(1)
                  << " " << (clock_gettime_times[1]/clock_gettime_times[0]) << "x" << std::endl;
    }

    double getpid_times[1] = {0};
    double gettimeofday_times[1] = {0};
    double clock_gettime_times[2] = {0};
    double getcpu_times[1] = {0};
};

int main() {
    VDSOBenchmark bench;
    bench.run();

    std::cout << "\nДля проверки vDSO используйте:\n"
              << "strace -e trace=clock_gettime,gettimeofday ./vdso_bench\n"
              << "Если syscall НЕ появляется = vDSO работает ✓" << std::endl;

    return 0;
}
