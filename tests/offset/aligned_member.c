#include <stddef.h>
struct Lane { char c; _Alignas(16) int number; char tail; };
_Static_assert(sizeof(struct Lane) == 32, "extent");
int main(void) { return offsetof(struct Lane, number) != 16 || offsetof(struct Lane, tail) != 20; }
