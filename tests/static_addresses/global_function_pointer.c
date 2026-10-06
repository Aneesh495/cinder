int value(int x) { return x+3; } int (*p)(int)=value; int main(void) { return p(86); }
