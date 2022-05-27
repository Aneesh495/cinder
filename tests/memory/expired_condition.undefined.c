int main(void) { int *p = 0; { int x = 7; p = &x; } if (p) return 1; return 0; }
