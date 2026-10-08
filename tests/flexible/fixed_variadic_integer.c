#include <stdarg.h>
struct S{long n;char data[];};static long read(int n,...){va_list ap;va_start(ap,n);struct S s=va_arg(ap,struct S);va_end(ap);return s.n;}int main(void){struct S s={79};return read(1,s)!=79;}
