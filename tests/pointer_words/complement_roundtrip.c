#include <stdint.h>
static uintptr_t flip(uintptr_t word) { return ~word; }
int main(void) { int value = 23; uintptr_t word = flip((uintptr_t)(void *)&value); return *(int *)(void *)flip(word); }
