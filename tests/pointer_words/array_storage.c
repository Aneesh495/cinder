#include <stdint.h>
int main(void) {
    int a = 11, b = 71;
    uintptr_t words[2] = {(uintptr_t)(void *)&a, (uintptr_t)(void *)&b};
    words[0] ^= (uintptr_t)3;
    words[1] ^= (uintptr_t)5;
    words[1] ^= (uintptr_t)5;
    return *(int *)(void *)words[1];
}
