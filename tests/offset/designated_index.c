#include <stddef.h>
struct A { char c; int v; };
int values[8] = { [offsetof(struct A, v)] = 19 };
int main(void) { return values[4] != 19 || values[3] != 0 || values[5] != 0; }
