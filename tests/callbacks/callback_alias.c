int add(int x) { return x + 9; } int sub(int x) { return x - 9; } int main(void) { int (*p)(int) = sub; int (**q)(int) = &p; *q = add; return p(28); }
