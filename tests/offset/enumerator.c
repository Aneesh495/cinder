#include <stddef.h>
struct A { char c; double v; };
enum { BYTE_OFFSET = offsetof(struct A, v), FOLLOWING = BYTE_OFFSET + 1 };
int main(void) { return BYTE_OFFSET != 8 || FOLLOWING != 9; }
