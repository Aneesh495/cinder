#include <stddef.h>
struct Inner { char c; int i; };
struct Outer { double d; struct Inner inner; };
int main(void) { return offsetof(struct Outer, inner.i) != 12; }
