#include <time.h>

_Static_assert(sizeof(clock_t) == sizeof(long), "LP64 process CPU clock extent");
_Static_assert(CLOCKS_PER_SEC == 1000000, "declared process CPU clock scale");

int main(void) {
    clock_t before = clock();
    volatile unsigned checksum = 0U;
    for (unsigned i = 0U; i < 10000U; ++i) checksum += i;
    clock_t after = clock();
    return before == (clock_t)-1 || after == (clock_t)-1 || after < before ||
           checksum != 49995000U;
}
