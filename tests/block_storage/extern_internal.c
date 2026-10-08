static int x=7; int f(void) { extern int x; return ++x; } int main(void) { return f()!=8 || x!=8; }
