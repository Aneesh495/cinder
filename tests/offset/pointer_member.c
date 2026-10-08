#include <stddef.h>
struct Links { char flag; int *next; int (*callback)(int); };
int main(void) { return offsetof(struct Links, next) != 8 || offsetof(struct Links, callback) != 16; }
