int main(void) { int x=42; unsigned long bits=(unsigned long)&x; return *(int *)bits; }
