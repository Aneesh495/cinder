#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
int main(void) {
    unsigned char *p = malloc(80); if (p == NULL) return 1;
    memset(p, 0, 80); for (size_t i = 0; i < 32; ++i) p[i] = (unsigned char)(i + 1);
    memmove(p + 3, p, 32); if (p[3] != 1 || p[34] != 32) { free(p); return 2; }
    unsigned char copy[32]; memcpy(copy, p + 3, 32);
    if (memcmp(copy, p + 3, 32) != 0 || memchr(copy, 17, 32) != copy + 16) { free(p); return 3; }
    void *grown = realloc(p, 160); if (grown == NULL) { free(p); return 4; }
    p = grown; int bad = p[3] != 1 || p[34] != 32 || (uintptr_t)p % _Alignof(max_align_t) != 0;
    free(p); return bad;
}
