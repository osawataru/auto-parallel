static void funcA(int *a, int n) {
  for (int i = 0; i < n; ++i)
    a[i] = i * 2;
}

static void funcB(int *a, int n) {
  for (int i = 0; i < n; ++i)
    a[i] += 1;
}

int main(void) {
  int a[16] = {0};
  funcA(a, 16);
  funcB(a, 16);
  return a[0] != 1 || a[15] != 31;
}
