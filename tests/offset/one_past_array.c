#include <stddef.h>
struct A { char c; int a[5]; int tail; };
int main(void) { return offsetof(struct A, a[5]) != offsetof(struct A, tail); }
