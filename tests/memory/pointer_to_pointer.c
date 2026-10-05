int main(void) { int x = 9, y = 42; int *p = &x; int **q = &p; *q = &y; return *p; }
