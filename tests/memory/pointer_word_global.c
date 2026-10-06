unsigned long bits; int main(void) { int x=41; bits=(unsigned long)&x; return *(int *)bits; }
