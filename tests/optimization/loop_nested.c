static unsigned sum(unsigned x, int n, int m) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < m; ++j) result += x ^ 5U;
    return result;
}
int main(void) { return (int)sum(3U, 2, 4); }
