#include <stdint.h>
int main(void) { return _Generic(INT32_C(1), int_least32_t: 0, default: 1) || _Generic(UINT32_C(1), uint_least32_t: 0, default: 1) || _Generic(INT64_C(1), int_least64_t: 0, default: 1) || _Generic(UINTMAX_C(1), uintmax_t: 0, default: 1); }
