#include <stdint.h>
static uintptr_t identity(uintptr_t word, unsigned shift) { return (word << shift) >> shift; }
int main(void) { int value = 43; return *(int *)(void *)identity((uintptr_t)(void *)&value, 0); }
