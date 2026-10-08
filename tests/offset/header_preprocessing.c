#include <stdint.h>
#include <limits.h>
#if UINT64_MAX != 18446744073709551615UL || INT_MIN != (-2147483647-1)
#error incorrect limits in preprocessing
#endif
#if UINT32_C(12) != 12U || INT64_C(19) != 19L
#error incorrect integer constant macros
#endif
int main(void) { return 0; }
