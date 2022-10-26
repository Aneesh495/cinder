#include <stdint.h>
int main(void) {
    int value = 31;
    volatile uintptr_t word = (uintptr_t)(void *)&value;
    word += UINTPTR_MAX;
    word += 1;
    return *(int *)(void *)word;
}
