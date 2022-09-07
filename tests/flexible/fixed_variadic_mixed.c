#include <stdarg.h>
struct S{long n;double weight;char data[];};static int read(int n,...){va_list ap;va_start(ap,n);struct S s=va_arg(ap,struct S);va_end(ap);return s.n==83&&s.weight==2.75;}int main(void){struct S s={83,2.75};return !read(1,s);}
