#include <stddef.h>
struct A { int a[5]; };
int f(int x);
int main(void) { return offsetof(struct A, a[sizeof(f())]); }
