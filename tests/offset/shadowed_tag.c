#include <stddef.h>
struct A { char c; int n; };
size_t outer = offsetof(struct A, n);
int main(void) { struct A { double d; int n; }; return outer != 4 || offsetof(struct A, n) != 8; }
