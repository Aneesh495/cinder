int f(void) { return 3; } int main(void) { static int x=f(); return x; }
