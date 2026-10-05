int main(void) { int a[4]; a[3] = 23; int *p = a + 4; --p; return *p; }
