static int x=3; int main(void) { static int x=2; { extern int x; return x; } }
