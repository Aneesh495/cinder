int f(int x) { return x+9; } int main(void) { unsigned long bits=(unsigned long)f; int (*fn)(int)=(int (*)(int))bits; return fn(24); }
