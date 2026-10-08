#include <stddef.h>
struct S { unsigned count:7; unsigned flag:1; char bytes[]; }; int main(void){struct S s={91,1};return s.count!=91 || s.flag!=1 || offsetof(struct S,bytes)!=1 || sizeof(s)!=4;}
