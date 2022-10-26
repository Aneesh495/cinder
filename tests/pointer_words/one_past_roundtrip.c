#include <stdint.h>
int main(void) {
    int values[3] = {1, 2, 3};
    uintptr_t word = (uintptr_t)(void *)(values + 3);
    int *end = (int *)(void *)(word ^ (uintptr_t)0);
    return end == values + 3 ? (int)(end - values) : 99;
}
