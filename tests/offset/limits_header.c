#include <limits.h>
_Static_assert(CHAR_BIT == 8 && CHAR_MIN == -128 && CHAR_MAX == 127, "character");
_Static_assert(INT_MIN == -2147483647 - 1 && LONG_MIN == LLONG_MIN, "signed");
int main(void) { unsigned long n = ULONG_MAX; return n + 1 != 0 || UINT_MAX != 4294967295U || USHRT_MAX != 65535; }
