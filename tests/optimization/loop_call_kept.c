static unsigned calls;
static unsigned next(unsigned x) { ++calls; return x * 3U; }
static unsigned sum(unsigned x, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += next(x);
    return result;
}
int main(void) { unsigned result = sum(3U, 4); return (int)(result + calls); }
