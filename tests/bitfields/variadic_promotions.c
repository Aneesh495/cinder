#include <stdarg.h>
struct S { unsigned a:3; signed b:5; _Bool c:1; }; int check(int n,...){va_list ap;va_start(ap,n);int a=va_arg(ap,int),b=va_arg(ap,int),c=va_arg(ap,int);va_end(ap);return a!=7 || b!=-9 || c!=1;} int main(void){struct S s={7,-9,1};return check(3,s.a,s.b,s.c);}
