static unsigned sum(unsigned x, unsigned y, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += x / y;
    return result;
}
int main(void) { return (int)sum(23U, 0U, 0); }
