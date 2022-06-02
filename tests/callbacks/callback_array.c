int add(int a, int b) { return a + b; } int sub(int a, int b) { return a - b; } int main(void) { int (*p[2])(int,int); p[0] = add; p[1] = sub; return p[0](7,9) + p[1](30,3); }
