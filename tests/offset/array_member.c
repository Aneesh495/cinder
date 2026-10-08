#include <stddef.h>
struct Samples { char tag; short values[7]; };
int main(void) { return offsetof(struct Samples, values[4]) != 10; }
