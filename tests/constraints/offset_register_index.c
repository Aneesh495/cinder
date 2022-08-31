#include <stddef.h>
struct A { int a[5]; };
int main(void) { register int n; return offsetof(struct A, a[sizeof(&n)]); }
