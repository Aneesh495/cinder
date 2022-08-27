#include <stdarg.h>
int pick(const int n,...) { va_list list;va_start(list,n);int v=va_arg(list,int);va_end(list);return n+v; } int main(void) { return pick(3,7)!=10; }
