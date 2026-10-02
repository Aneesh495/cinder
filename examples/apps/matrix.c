int kernel(int n) { int total = 0; int i = 0; while (i < n) { int j = 0; while (j < n) { total = total + i * j + 1; j++; } i++; } return total & 255; }
int main(void) { return kernel(5); }
