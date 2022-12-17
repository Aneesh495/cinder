#ifndef DEPTH
#define DEPTH 254
#endif
static int descend(int n) {
    if (n == 0) return 0;
    int value = descend(n - 1);
    return value + 1;
}
int main(void) { return descend(DEPTH); }
