int f(void) { enum { B=4 }; static int x=B+2; return x++; } int main(void) { return f()!=6 || f()!=7; }
