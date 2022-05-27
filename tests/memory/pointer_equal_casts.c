int main(void) { int x; const int *p = &x; void *q = &x; return p == q; }
