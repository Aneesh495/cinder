int value(int); int (*p)(int)=value; int value(int actual) { return actual+7; } int main(void) { return p(94); }
