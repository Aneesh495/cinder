static unsigned sum(unsigned x, unsigned y, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += (x + y) * (x ^ 5U);
    return result;
}
int main(void) { return (int)sum(3U, 2U, 3); }
