void set(int *p) { *p = 41; } int main(void) { int x = 0; void (*fn)(int *) = set; fn(&x); return x; }
