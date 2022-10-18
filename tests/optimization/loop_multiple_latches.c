static unsigned sum(unsigned x, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) {
        if ((i & 1) != 0) continue;
        result += x * 7U;
    }
    return result;
}
int main(void) { return (int)sum(3U, 5); }
