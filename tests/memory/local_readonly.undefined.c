int main(void) { const int x = 7; int *p = (int *)&x; *p = 8; return x; }
