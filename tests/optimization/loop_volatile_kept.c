static volatile unsigned value = 3U;
static unsigned sum(int n) {
    unsigned result = 0;
    for (int i = 0; i < n; ++i) result += value++;
    return result;
}
int main(void) { return (int)sum(4); }
