#include <stddef.h>
struct S{int n; _Alignas(16) char data[];};_Static_assert(sizeof(struct S)==16 && offsetof(struct S,data)==16 && _Alignof(struct S)==16,"aligned flexible field");int main(void){return 0;}
