static unsigned sum(unsigned x, int n) {
    unsigned result = 0;
    do { result += x * 7U; } while (--n > 0);
    return result;
}
int main(void) { return (int)sum(3U, 3); }
