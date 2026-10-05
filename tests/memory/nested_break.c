int main(void) { int sum = 0; for (int i = 0; i < 3; ++i) { int x = i; int *p = &x; while (1) { int y = *p + 6; int *q = &y; sum += *q; break; } } return sum; }
