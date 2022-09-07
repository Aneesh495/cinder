#include <stddef.h>
struct S{char tag; short data[][3];};_Static_assert(sizeof(struct S)==2 && offsetof(struct S,data)==2,"array element");int main(void){return 0;}
