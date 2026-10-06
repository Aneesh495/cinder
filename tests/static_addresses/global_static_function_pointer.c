static int value(int x) { return x+2; } static int (*p)(int)=value; int main(void) { return p(107); }
