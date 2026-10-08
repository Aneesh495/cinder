int f(void) { static int x=4; static int *p=&x; return ++*p; } int main(void) { return f()!=5 || f()!=6; }
