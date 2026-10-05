int a(int x) { return x; } int b(int x) { return x; } int main(void) { int (*p)(int) = a; int (*q)(int) = b; return (p == a) + (p != q) + (q != 0); }
