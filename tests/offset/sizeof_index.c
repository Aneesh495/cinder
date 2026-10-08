#include <stddef.h>
struct A { short p; int a[12]; };
int main(void) { int local = 9; return offsetof(struct A, a[sizeof(local)]) != 20 || local != 9; }
