#include <stddef.h>
struct A { char c; int v; };
int select(int v) { switch (v) { case offsetof(struct A, v): return 7; default: return 3; } }
int main(void) { return select(4) != 7 || select(0) != 3; }
