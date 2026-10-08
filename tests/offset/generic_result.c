#include <stddef.h>
struct A { int a; int b; };
int main(void) { return _Generic(offsetof(struct A, b), unsigned long: 0, default: 1); }
