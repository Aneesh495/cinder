#include <stdint.h>
int main(void) { int value = 83; uintptr_t first = (uintptr_t)(void *)&value; uintptr_t second = first; uintptr_t third = second ^ (uintptr_t)0; return *(int *)(void *)third; }
