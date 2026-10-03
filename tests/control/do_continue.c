int main(void) { int x = 0; int sum = 0; do { x++; if (x == 3) continue; sum = sum + x; } while (x < 5); return sum; }
