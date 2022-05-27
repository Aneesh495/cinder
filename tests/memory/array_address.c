int main(void) { int a[2][2]; a[1][0] = 17; int (*p)[2][2] = &a; return (*p)[1][0]; }
