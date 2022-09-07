#include <stddef.h>
struct S{long n; char tag; char data[];};_Static_assert(sizeof(struct S)==16 && offsetof(struct S,data)==9,"tail padding");int main(void){return 0;}
