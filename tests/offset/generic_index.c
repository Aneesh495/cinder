#include <stddef.h>
struct A { char c; long a[4]; };
int main(void) { return offsetof(struct A, a[_Generic(0, int: 2, default: 1)]) != 24; }
