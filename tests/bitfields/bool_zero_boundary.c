#include <stddef.h>
struct S { _Bool a:1; _Bool :0; _Bool b:1; char c; }; int main(void){struct S s={1,1,'R'};return s.a!=1 || s.b!=1 || s.c!='R' || offsetof(struct S,c)!=2 || sizeof(s)!=3;}
