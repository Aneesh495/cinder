const int x = 17; int main(void) { int *p = (int *)&x; *p = 9; return x; }
