#include <stddef.h>
struct A { int a[5]; };
int main(void) { return offsetof(struct A, a[1.0]); }
