int *choose(int *a, int *b, int flag) { return flag ? a : b; } int main(void) { int x = 7, y = 26; return *choose(&x, &y, 0); }
