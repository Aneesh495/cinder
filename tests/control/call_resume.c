static int sum(int n) {
    if (n == 0) return 0;
    int previous = sum(n - 1);
    return previous + n;
}
int main(void) {
    int total = 0;
    for (int i = 1; i <= 6; ++i) total += sum(i);
    return total;
}
