int x=5; int f(void) { int x=9; { extern int x; ++x; } return x; } int main(void) { return f()!=9 || x!=6; }
