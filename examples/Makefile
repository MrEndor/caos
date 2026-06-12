# CAOS examples — рабочие сниппеты к 21 экзаменационному вопросу.
#
#   make            собрать всё (demo + tests + benches)
#   make demos      только иллюстративные примеры (серверы, фибры, callback hell)
#   make tests      собрать GoogleTest-бинарники
#   make benches    собрать GoogleBenchmark-бинарники
#   make test       собрать и ЗАПУСТИТЬ все тесты
#   make bench      собрать и ЗАПУСТИТЬ все бенчмарки
#   make qNN        один пример по номеру вопроса
#   make clean
#
# Требования: g++ >= 11 (C++20), liburing-dev (q12), GoogleTest, GoogleBenchmark,
# x86-64 Linux (futex/epoll/io_uring; q15_asm — ассемблер x86-64 System V).

CXX      ?= g++
CXXSTD   := -std=c++20
CXXFLAGS := $(CXXSTD) -O2 -Wall -Wextra
PTHREAD  := -pthread
ATOMIC   := -latomic
GTEST    := -lgtest -lgtest_main -pthread
GBENCH   := -lbenchmark -pthread

BIN := bin
$(shell mkdir -p $(BIN))

# ── иллюстративные demo (запускаются, печатают вывод) ──
DEMO_TARGETS := \
  q01_server q02_server q02_client q10_server \
  q11_select q11_poll q11_epoll q12_server \
  q13_demo q15_ucontext q15_asm

# ── GoogleTest (проверка корректности) ──
TEST_TARGETS := \
  q03_test q04_test q05_test q07_test q08_test q09_test \
  q14_test q16_test q16_lowering_test q16_symmetric_test q18_test q19_test q20_test q21_test

# ── GoogleBenchmark (замер времени/throughput) ──
BENCH_TARGETS := q05_bench q06_bench q17_bench

all: demos tests benches
	@echo "── всё собрано в $(BIN)/ ──"

demos:   $(DEMO_TARGETS)
tests:   $(TEST_TARGETS)
benches: $(BENCH_TARGETS)

# ── demo: сети ──
q01_server: ; $(CXX) $(CXXFLAGS) q01_sync_tcp_server/server.cpp      -o $(BIN)/$@
q02_server: ; $(CXX) $(CXXFLAGS) q02_threaded_tcp/server.cpp         -o $(BIN)/$@ $(PTHREAD)
q02_client: ; $(CXX) $(CXXFLAGS) q02_threaded_tcp/client.cpp         -o $(BIN)/$@
q10_server: ; $(CXX) $(CXXFLAGS) q10_nonblocking_busyloop/server.cpp -o $(BIN)/$@
q11_select: ; $(CXX) $(CXXFLAGS) q11_epoll_server/select_server.cpp  -o $(BIN)/$@
q11_poll:   ; $(CXX) $(CXXFLAGS) q11_epoll_server/poll_server.cpp    -o $(BIN)/$@
q11_epoll:  ; $(CXX) $(CXXFLAGS) q11_epoll_server/server.cpp         -o $(BIN)/$@
q12_server: ; $(CXX) $(CXXFLAGS) q12_io_uring_server/server.cpp      -o $(BIN)/$@ -luring

# ── demo: async / фибры ──
q13_demo:     ; $(CXX) $(CXXFLAGS) q13_future_promise/future_demo.cpp -o $(BIN)/$@ $(PTHREAD)
q15_ucontext: ; $(CXX) $(CXXFLAGS) q15_fibers/fiber_ucontext.cpp      -o $(BIN)/$@
q15_asm:      ; $(CXX) $(CXXFLAGS) q15_fibers/fiber_asm.cpp q15_fibers/context_switch.S -o $(BIN)/$@

# ── GoogleTest ──
q03_test: ; $(CXX) $(CXXFLAGS) q03_cpp_mutexes/mutexes_test.cpp          -o $(BIN)/$@ $(GTEST)
q04_test: ; $(CXX) $(CXXFLAGS) q04_condvar_semaphore/condvar_sem_test.cpp -o $(BIN)/$@ $(GTEST)
q05_test: ; $(CXX) $(CXXFLAGS) q05_thread_pool/thread_pool_test.cpp       -o $(BIN)/$@ $(GTEST)
q07_test: ; $(CXX) $(CXXFLAGS) q07_futex_mutex/futex_mutex_test.cpp       -o $(BIN)/$@ $(GTEST)
q08_test: ; $(CXX) $(CXXFLAGS) q08_three_state_mutex/three_state_test.cpp -o $(BIN)/$@ $(GTEST)
q09_test: ; $(CXX) $(CXXFLAGS) q09_condvar_futex/condvar_futex_test.cpp   -o $(BIN)/$@ $(GTEST)
q14_test: ; $(CXX) $(CXXFLAGS) q14_future_impl/my_future_test.cpp         -o $(BIN)/$@ $(GTEST)
q16_test: ; $(CXX) $(CXXFLAGS) q16_coroutines/generator_task_test.cpp     -o $(BIN)/$@ $(GTEST)
q16_lowering_test: ; $(CXX) $(CXXFLAGS) q16_coroutines/coroutine_lowering_test.cpp -o $(BIN)/$@ $(GTEST)
q16_symmetric_test: ; $(CXX) $(CXXFLAGS) q16_coroutines/symmetric_transfer_test.cpp -o $(BIN)/$@ $(GTEST)
q18_test: ; $(CXX) $(CXXFLAGS) q18_treiber_stack/treiber_stack_test.cpp   -o $(BIN)/$@ $(GTEST) $(ATOMIC)
q19_test: ; $(CXX) $(CXXFLAGS) q19_ms_queue/ms_queue_test.cpp             -o $(BIN)/$@ $(GTEST) $(ATOMIC)
q20_test: ; $(CXX) $(CXXFLAGS) q20_atomic_shared_ptr/atomic_sp_test.cpp   -o $(BIN)/$@ $(GTEST) $(ATOMIC)
q21_test: ; $(CXX) $(CXXFLAGS) q21_memory_model/store_buffering_test.cpp  -o $(BIN)/$@ $(GTEST)

# ── GoogleBenchmark ──
q05_bench: ; $(CXX) $(CXXFLAGS) q05_thread_pool/thread_pool_bench.cpp     -o $(BIN)/$@ $(GBENCH)
q06_bench: ; $(CXX) $(CXXFLAGS) q06_atomic_spinlock/spinlock_bench.cpp    -o $(BIN)/$@ $(GBENCH) $(ATOMIC)
q17_bench: ; $(CXX) $(CXXFLAGS) q17_false_sharing/false_sharing_bench.cpp -o $(BIN)/$@ $(GBENCH)

# ── запуск ──
test: tests
	@for t in $(TEST_TARGETS); do echo "── $$t ──"; ./$(BIN)/$$t || exit 1; done

bench: benches
	@for b in $(BENCH_TARGETS); do echo "── $$b ──"; ./$(BIN)/$$b; done

# ── алиасы по номеру вопроса ──
q01: q01_server
q02: q02_server q02_client
q03: q03_test
q04: q04_test
q05: q05_test q05_bench
q06: q06_bench
q07: q07_test
q08: q08_test
q09: q09_test
q10: q10_server
q11: q11_select q11_poll q11_epoll
q12: q12_server
q13: q13_demo
q14: q14_test
q15: q15_ucontext q15_asm
q16: q16_test q16_lowering_test q16_symmetric_test
q17: q17_bench
q18: q18_test
q19: q19_test
q20: q20_test
q21: q21_test

clean:
	rm -rf $(BIN)

.PHONY: all demos tests benches test bench clean \
        $(DEMO_TARGETS) $(TEST_TARGETS) $(BENCH_TARGETS) \
        q01 q02 q03 q04 q05 q06 q07 q08 q09 q10 q11 \
        q12 q13 q14 q15 q16 q17 q18 q19 q20 q21
