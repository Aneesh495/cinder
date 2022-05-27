int main(void) { int a[3]; a[0] = 9; a[1] = 11; int i = 0; a[i++] += 10; return a[0] + a[1] + i; }
