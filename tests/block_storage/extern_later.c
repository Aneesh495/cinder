int f(void) { extern int x; return ++x; } int x=3; int main(void) { return f()!=4 || x!=4; }
