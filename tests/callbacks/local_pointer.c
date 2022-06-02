int add(int a, int b) { return a + b; } int main(void) { int (*p)(int, int) = add; return p(17, 22); }
