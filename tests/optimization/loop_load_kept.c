static unsigned sum(const unsigned *p, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += *p;
    return result;
}
int main(void) { unsigned x = 9U; return (int)sum(&x, 3); }
