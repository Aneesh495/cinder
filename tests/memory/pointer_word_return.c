unsigned long identity(unsigned long bits) { return bits; } int main(void) { int x=35; unsigned long bits=identity((unsigned long)&x); return *(int *)bits; }
