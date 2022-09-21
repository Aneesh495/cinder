#include <stddef.h>
struct S { unsigned n:3; }; unsigned long n=offsetof(struct S,n);
