#include <stdint.h>
struct Box { uintptr_t word; double marker; };
static struct Box encode(struct Box box) { box.word ^= (uintptr_t)0x55; return box; }
int main(void) {
    int value = 61;
    struct Box box = {(uintptr_t)(void *)&value, 3.0};
    struct Box copy = encode(box);
    copy.word ^= (uintptr_t)0x55;
    return copy.marker == 3.0 ? *(int *)(void *)copy.word : 1;
}
