#include <stdint.h>
static uintptr_t identity(uintptr_t word, uintptr_t mask) { return (word & mask) | (uintptr_t)0; }
int main(void) { int value = 41; return *(int *)(void *)identity((uintptr_t)(void *)&value, UINTPTR_MAX); }
