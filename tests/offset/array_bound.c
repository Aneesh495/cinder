#include <stddef.h>
struct Header { char magic; long length; };
int slots[offsetof(struct Header, length)];
int main(void) { return sizeof(slots) != 32; }
