int main(void) { int a[2][3]; a[0][2] = 19; a[1][1] = 28; int (*p)[3] = a; return p[0][2] + p[1][1]; }
