#include <stdarg.h>
int invalid(int last) { va_list list; va_start(list,last); va_end(list); return 0; }
