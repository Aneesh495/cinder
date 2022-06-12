int main(void) { int x=3; unsigned long bits=(unsigned long)&x; ((unsigned char *)&bits)[0]^=1; return *(int *)bits; }
