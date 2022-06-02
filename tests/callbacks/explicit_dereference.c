int twice(int x) { return x * 2; } int main(void) { int (*p)(int) = &twice; return (*p)(15) + 1; }
