extern int (*p)[]; int (*p)[3]; int main(void) { int a[3]; a[2]=57; p=&a; return (*p)[2]; }
