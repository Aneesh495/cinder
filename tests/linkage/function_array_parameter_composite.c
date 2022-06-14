int value(int (*p)[]); int value(int (*p)[3]) { return (*p)[1]; } int main(void) { int a[3]; a[1]=71; return value(&a); }
