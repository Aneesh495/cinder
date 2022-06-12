int main(void) { int x=31; unsigned long bits=(unsigned long)&x; unsigned long *alias=&bits; return *(int *)*alias; }
