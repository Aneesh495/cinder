
int main(void) { int x = 3; unsigned long n = sizeof(x++); return x != 3 || n != 4; }
