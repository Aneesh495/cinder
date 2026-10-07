#include <stdarg.h>
int invalid(int first,int last,...) { va_list list; va_start(list,first); va_end(list); return 0; }
