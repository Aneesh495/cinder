#include <stddef.h>
struct A { int a[5]; };
int main(void) { return offsetof(struct A, a[_Generic(0, int: 1, default: 1.0 % 2)]); }
