#include <stdint.h>
int main(void) {
    int value = 13;
    intptr_t signed_word = (intptr_t)(void *)&value;
    uintptr_t unsigned_word = (uintptr_t)signed_word;
    return *(int *)(void *)unsigned_word;
}
