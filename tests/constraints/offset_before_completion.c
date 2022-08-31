#include <stddef.h>
struct A;
enum { N = offsetof(struct A, x) };
struct A { int x; };
int main(void) { return N; }
