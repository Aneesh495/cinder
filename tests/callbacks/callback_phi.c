int add(int x) { return x + 1; } int twice(int x) { return x * 2; } int main(void) { int (*p)(int) = add; for (int i=0; i<3; ++i) { if (i == 1) p = twice; } return p(21); }
