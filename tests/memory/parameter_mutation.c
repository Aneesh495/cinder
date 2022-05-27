void add(int *p, int n) { *p += n; } int main(void) { int x = 10; add(&x, 29); return x; }
