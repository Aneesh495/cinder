int read(unsigned long bits) { return *(int *)bits; } int main(void) { int x=23; unsigned long bits=(unsigned long)&x; return read(bits); }
