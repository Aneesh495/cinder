#include <stddef.h>
struct S { char tag; int code; double value; };
_Static_assert(offsetof(struct S, tag) == 0, "first");
_Static_assert(offsetof(struct S, code) == 4, "padding");
_Static_assert(offsetof(struct S, value) == 8, "double");
int main(void) { return offsetof(struct S, value) != 8; }
