int step(int n) { if (n == 0) return 0; int (*recur)(int) = step; return n + recur(n - 1); } int main(void) { return step(9); }
