int main(void) { int a[3]; int *p=a+3; unsigned long bits=(unsigned long)p; return (int *)bits==p; }
