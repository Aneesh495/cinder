int twice(int x) { return x * 2; } int main(void) { int (*fn)(int,int) = (int (*)(int,int))twice; return fn(7,9); }
