int x=2; int f(void) { static int x=3; return ++x; } int main(void) { return f()!=4 || x!=2; }
