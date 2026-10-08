#include <stddef.h>
struct A { int *p; };
int main(void) { return offsetof(struct A, p[1]); }
