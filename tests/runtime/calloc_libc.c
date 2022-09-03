#include <stdlib.h>
int main(void) { long *p = calloc(13, sizeof(*p)); if (!p) return 1; int bad = 0; for (int i = 0; i < 13; ++i) bad |= p[i] != 0; p[12] = 71; bad |= p[12] != 71; free(p); return bad; }
