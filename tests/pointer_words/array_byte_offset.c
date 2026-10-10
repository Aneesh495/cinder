#include <stdint.h>
int main(void) {
    unsigned char bytes[4] = {3, 5, 73, 7};
    uintptr_t word = (uintptr_t)(void *)bytes;
    word += 2;
    return *(unsigned char *)(void *)word;
}
