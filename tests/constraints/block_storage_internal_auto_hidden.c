static int x=3; int main(void) { int x=2; { extern int x; return x; } }
