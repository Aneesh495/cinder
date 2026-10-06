extern int (*callback)(int (*)[]); int (*callback)(int (*)[3]); int value(int (*p)[3]) { return (*p)[0]; } int main(void) { int a[3]; a[0]=73; callback=value; return callback(&a); }
