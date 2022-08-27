#include <stdarg.h>
typedef int *P;int pick(P p,...) { va_list list;va_start(list,p);int v=va_arg(list,int);va_end(list);return *p+v; } int main(void) { int n=3;return pick(&n,7)!=10; }
