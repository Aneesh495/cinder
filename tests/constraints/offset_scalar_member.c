#include <stddef.h>
struct A { int x; };
int main(void) { return offsetof(struct A, x.y); }
