int value(int x) { return x+5; } int (*p)(int)=&value; int main(void) { return p(92); }
