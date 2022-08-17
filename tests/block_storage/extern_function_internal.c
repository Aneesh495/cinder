static int f(void) { return 3; } int main(void) { extern int f(void); return f()!=3; }
