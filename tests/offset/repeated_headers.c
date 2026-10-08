#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
int main(void) { size_t n = SIZE_MAX; bool ok = (n == UINTPTR_MAX); return !ok || PTRDIFF_MAX != LONG_MAX || INTMAX_MAX != INT64_MAX; }
