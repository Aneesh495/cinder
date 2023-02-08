#include "macros.h"
#line 700 "explorer-logical.c"
volatile int explorer_gate = 3;
static int accumulate(int n) {
    int total = 0;
    for (int i = 0; i < n; ++i) {
        int invariant = (n + 5) * 8;
        total += invariant + (i & 7);
    }
    return total;
}
int main(void) {
    int value = FOUR(explorer_gate);
    if (value != 12 || accumulate(4) != 294) return 1;
    return 0;
}
