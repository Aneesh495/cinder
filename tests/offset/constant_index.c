#include <stddef.h>
struct A { short x; int a[10]; };
enum { PICK = 3 };
int main(void) { return offsetof(struct A, a[(PICK << 1) - 1]) != 24; }
