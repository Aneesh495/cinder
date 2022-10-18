static int sum(int x, int n) {
    int result = 0;
    for (int i = 0; i < n; ++i) result += x * 7;
    return result;
}
int main(void) { return sum(3, 4); }
