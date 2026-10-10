#include <stdint.h>
static volatile uintptr_t saved;
int main(void) { int value = 67; saved = (uintptr_t)(void *)&value; saved ^= (uintptr_t)7; saved ^= (uintptr_t)7; return *(int *)(void *)saved; }
