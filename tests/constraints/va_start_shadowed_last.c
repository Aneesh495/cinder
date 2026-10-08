#include <stdarg.h>
int f(int n,...) { va_list list;{ int n=3;va_start(list,n); }va_end(list);return n; }
