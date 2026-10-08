#include <stdarg.h>
int f(register int n,...) { va_list list;va_start(list,n);va_end(list);return 0; }
