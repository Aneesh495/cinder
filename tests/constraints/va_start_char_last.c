#include <stdarg.h>
int f(char n,...) { va_list list;va_start(list,n);va_end(list);return 0; }
