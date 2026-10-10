#include <stdint.h>
int main(void) { const int value = 79; uintptr_t word = (uintptr_t)(const void *)&value; word ^= (uintptr_t)17; word ^= (uintptr_t)17; return *(const int *)(const void *)word; }
