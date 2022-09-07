#include <stddef.h>
struct S{int n; double data[];}; _Static_assert(sizeof(struct S)==8 && offsetof(struct S,data)==8 && _Alignof(struct S)==8,"double header"); int main(void){return 0;}
