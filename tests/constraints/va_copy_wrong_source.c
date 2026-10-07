#include <stdarg.h>
int invalid(int last,...) { va_list list; int source; va_copy(list,source); va_end(list); return 0; }
