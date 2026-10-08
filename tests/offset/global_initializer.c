#include <stddef.h>
struct A { char c; double d; long k; };
size_t offsets[3] = { offsetof(struct A, c), offsetof(struct A, d), offsetof(struct A, k) };
int main(void) { return offsets[0] || offsets[1] != 8 || offsets[2] != 16; }
