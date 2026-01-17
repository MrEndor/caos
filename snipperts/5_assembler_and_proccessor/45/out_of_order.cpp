//
// Created by alex on 1/16/26.
//

#include <iostream>
#include <thread>

int a = 0, b = 0;

void thread1() {
  a = 10;
  b = 42;
}

void thread2() {
  while(b != 42) {
    std::cout << a;
  }
}

int main() {
  std::thread t2(thread2), t1(thread1);

  t1.join();
  t2.join();
}