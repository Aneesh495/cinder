static unsigned sum(unsigned x, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += (x & 15U) ^ 5U;
    return result;
}
int main(void) { return (int)sum(19U, 3); }
