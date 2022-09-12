#include <stddef.h>
struct S{struct{int n;};double data[];};_Static_assert(sizeof(struct S)==8&&offsetof(struct S,data)==8,"anonymous named prefix");int main(void){struct S s={.n=67};return s.n!=67;}
