int f(void) { _Alignas(16) static int x=3; return (((unsigned long)&x)&15UL)!=0 || x!=3; } int main(void) { return f(); }
