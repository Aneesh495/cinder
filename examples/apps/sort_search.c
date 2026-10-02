int score(int n) { int best = 0; int i = 0; while (i < n) { int candidate = (n - i) * (i + 1); if (candidate > best) best = candidate; i++; } return best & 255; }
int main(void) { return score(10); }
