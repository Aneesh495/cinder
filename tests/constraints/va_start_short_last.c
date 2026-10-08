#include <stdarg.h>
int f(short n,...) { va_list list;va_start(list,n);va_end(list);return 0; }
