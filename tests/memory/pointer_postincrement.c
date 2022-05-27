int main(void) { int a[3]; a[0] = 7; a[1] = 21; int *p = a; int x = *p++; return x * 3 + *p; }
