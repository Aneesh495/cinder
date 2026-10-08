#include <stddef.h>
struct S{int n; char data[];}; _Static_assert(sizeof(struct S)==4 && offsetof(struct S,data)==4 && _Alignof(struct S)==4,"byte header"); int main(void){struct S s={7};return s.n!=7;}
