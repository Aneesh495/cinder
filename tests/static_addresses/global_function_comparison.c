int value(int x) { return x; } int (*p)(int)=value; int main(void) { return p==value; }
