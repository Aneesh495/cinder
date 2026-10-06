int main(void) { int x=29; void *p=&x; unsigned long bits=(unsigned long)p; return *(int *)(void *)bits; }
