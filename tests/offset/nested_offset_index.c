#include <stddef.h>
struct Inner { char c; int n; };
struct Outer { char tag; short a[8]; };
int main(void) { return offsetof(struct Outer, a[offsetof(struct Inner, n)]) != 10; }
