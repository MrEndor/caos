#include <iostream>

int factorial(int n) {
  if (n <= 1)
    return 1;
  return n * factorial(n - 1);
}

int main() {
  int x = 5;
  int result = factorial(x);
  std::cout << "Factorial of " << x << " is " << result << std::endl;

  int arr[3] = {10, 20, 30};
  for (int i = 0; i < 3; i++) {
    std::cout << "arr[" << i << "] = " << arr[i] << std::endl;
  }
}
