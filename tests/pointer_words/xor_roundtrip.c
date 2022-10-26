#include <stdint.h>
int main(void) {
    int value = 19;
    volatile uintptr_t word = (uintptr_t)(void *)&value;
    word ^= (uintptr_t)0x5a5a;
    word ^= (uintptr_t)0x5a5a;
    return *(int *)(void *)word;
}
