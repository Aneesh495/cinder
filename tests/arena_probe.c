#include "cinder.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CinderArena arena; cinder_arena_init(&arena, 4096U);
    for (size_t alignment = 1U; alignment <= 4096U; alignment *= 2U) {
        for (size_t size = 4093U; size < 8193U; size += 511U) {
            unsigned char *memory = cinder_arena_alloc(&arena, size, alignment);
            if ((uintptr_t)memory % alignment != 0U) return 1;
            memset(memory, 0xAD, size);
            if (memory[0] != 0xADU || memory[size - 1U] != 0xADU) return 1;
        }
    }
    cinder_arena_destroy(&arena);
    return 0;
}
