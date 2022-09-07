#include <stddef.h>
struct S{char n; int *data[];};_Static_assert(sizeof(struct S)==8 && offsetof(struct S,data)==8,"pointer element");int main(void){return 0;}
