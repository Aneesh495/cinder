#include <stddef.h>
struct A;
int main(void) { return offsetof(struct A, value); }
