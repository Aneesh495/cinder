int x=4; int f(void) { extern int x; extern int x; return ++x; } int main(void) { return f()!=5 || x!=5; }
