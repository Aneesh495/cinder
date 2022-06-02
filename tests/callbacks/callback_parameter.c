int add(int x) { return x + 9; } int invoke(int (*p)(int), int x) { return p(x); } int main(void) { return invoke(add, 33); }
