#include <stdint.h>
struct Item { const int *pointer; };
static const int *select(const struct Item *item, const int *fallback) {
    return item == 0 ? fallback : item->pointer;
}
int main(void) {
    const int value = 97;
    const struct Item item = {&value};
    uintptr_t word = (uintptr_t)(const void *)select(&item, 0);
    return *(const int *)(const void *)word;
}
