#include <stdint.h>
_Static_assert(sizeof(int8_t) == 1 && sizeof(uint16_t) == 2, "small");
_Static_assert(sizeof(int32_t) == 4 && sizeof(uint64_t) == 8, "large");
int main(void) { uint64_t a = UINT64_C(9223372036854775808); int64_t b = INT64_MIN; return a != (uint64_t)b || UINT32_MAX != UINT32_C(4294967295); }
