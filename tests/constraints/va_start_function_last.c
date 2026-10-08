#include <stdarg.h>
int f(int n(void),...) { va_list list;va_start(list,n);va_end(list);return 0; }
