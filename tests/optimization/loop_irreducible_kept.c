static unsigned sum(unsigned x, int n, int entry) {
    unsigned result = 0;
    if (entry != 0) goto second;
first:
    result += x * 3U;
    if (--n <= 0) return result;
second:
    result += x * 5U;
    if (--n > 0) goto first;
    return result;
}
int main(void) { return (int)sum(3U, 3, 0); }
