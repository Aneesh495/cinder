#include <stdint.h>
static uintptr_t select(int condition, uintptr_t a, uintptr_t b) {
    uintptr_t selected;
    if (condition) selected = a; else selected = b;
    return selected ^ (uintptr_t)0;
}
int main(void) { int a = 13, b = 47; return *(int *)(void *)select(0, (uintptr_t)(void *)&a, (uintptr_t)(void *)&b); }
