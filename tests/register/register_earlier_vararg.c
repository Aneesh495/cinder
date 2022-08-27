#include <stdarg.h>
int pick(register int a,int n,...) { va_list list;va_start(list,n);int v=va_arg(list,int);va_end(list);return a+n+v; } int main(void) { return pick(2,3,7)!=12; }
