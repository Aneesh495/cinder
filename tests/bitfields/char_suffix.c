#include <stddef.h>
struct S { unsigned a:3; char c; unsigned b:5; }; int main(void){struct S s={5,'K',17};return s.a!=5 || s.c!='K' || s.b!=17 || offsetof(struct S,c)!=1 || sizeof(s)!=4;}
