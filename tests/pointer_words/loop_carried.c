#include <stdint.h>
int main(void) {
    int value = 89;
    uintptr_t word = (uintptr_t)(void *)&value;
    for (int i = 0; i < 4; ++i) word ^= (uintptr_t)0x32;
    return *(int *)(void *)word;
}
