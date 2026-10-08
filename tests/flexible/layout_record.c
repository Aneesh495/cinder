#include <stddef.h>
struct E{char c; int n;}; struct S{char tag; struct E data[];}; _Static_assert(sizeof(struct S)==4 && offsetof(struct S,data)==4,"record element");int main(void){return 0;}
