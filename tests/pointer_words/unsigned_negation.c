#include <stdint.h>
static uintptr_t negate(uintptr_t word) { return -word; }
int main(void) { int value = 29; return *(int *)(void *)negate(negate((uintptr_t)(void *)&value)); }
