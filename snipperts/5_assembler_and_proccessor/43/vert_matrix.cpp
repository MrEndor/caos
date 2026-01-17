//
// Created by alex on 1/16/26.
//

#include <cstdio>
#include <cstdlib>
#include <vector>

int main() {
  int size = 10000;
  std::vector matrix(size, std::vector<int>(size));

  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      matrix[i][j] = rand();
    }
  }

  size_t sum = 0;
  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size; j++) {
      sum += matrix[j][i];
    }
  }

  printf("%li\n", sum);
}
