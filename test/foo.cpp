#include <iostream>

int

bar(int i) {
  if (i & 1) {
    return i & (-1);
  } else {
    return i;
  }
}

void
foo(int *array, int n, int *result) {
  for (int i = 0; i < n - 2; ++i) {
    result[i] = (array[i] + array[i + 1] + array[i + 2]) / 3;
  }
}

int main() {
  const int N = 11;
  int array[N] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
  int result[N - 2] = {};
  foo(array, 11, result);

  return 0;
}
