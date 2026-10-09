static unsigned sum(unsigned long x, int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += (unsigned)x;
    return result;
}
int main(void) { return (int)sum(0x100000009UL, 3); }
