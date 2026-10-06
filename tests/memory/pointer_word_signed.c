int main(void) { int x=37; long bits=(long)&x; return *(int *)bits; }
