int main(void) { int sum = 0; for (int i = 0; i < 8; ++i) { int x = i; int *p = &x; if (i == 2) continue; sum += *p; if (i == 6) break; } return sum + 9; }
