#include <stdint.h>
static uintptr_t identity(uintptr_t word, uintptr_t factor) { return word * factor / factor; }
int main(void) { int value = 37; return *(int *)(void *)identity((uintptr_t)(void *)&value, 1); }
