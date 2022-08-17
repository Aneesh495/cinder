static int x=5; int main(void) { extern int x; { extern int x; ++x; } return x!=6; }
