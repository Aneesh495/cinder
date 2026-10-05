int main(void) { int a[3]; int *p = a + 1; return (p > a) + (p < a + 2) + (a + 3 >= p); }
