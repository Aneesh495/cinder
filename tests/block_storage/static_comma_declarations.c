int f(void) { static int x=2,y=3; return ++x + ++y; } int main(void) { return f()!=7 || f()!=9; }
