#include <stddef.h>
struct A { char c; int v[3]; };
int main(void) { return sizeof(offsetof(struct A, v[2])) != sizeof(size_t); }
