#include <stddef.h>
struct S { char c; unsigned a:3; unsigned b:5; char d; }; int main(void){struct S s={'A',6,29,'Z'};s.a=3;return s.c!='A' || s.b!=29 || s.d!='Z' || offsetof(struct S,d)!=2 || sizeof(s)!=4;}
