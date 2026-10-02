int hash_step(int h, int value) { return ((h * 33) ^ value) & 255; }
int main(void) { int h = 0; int i = 1; while (i <= 8) { h = hash_step(h, i); i++; } return h; }
