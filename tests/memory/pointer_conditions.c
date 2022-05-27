int main(void) { int x = 35; int *p = &x; int *q = 0; if (p && !q) return *p; return 7; }
