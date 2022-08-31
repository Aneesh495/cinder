#include <stdint.h>
_Static_assert(sizeof(int_least8_t) == 1 && sizeof(int_least16_t) == 2, "least");
_Static_assert(sizeof(int_fast16_t) >= 2 && sizeof(int_fast32_t) >= 4, "fast");
int main(void) { int_fast16_t n = INT_FAST16_MAX; uint_least32_t m = UINT_LEAST32_MAX; return n <= 0 || m != UINT32_MAX || INT_FAST64_MIN != INT64_MIN; }
