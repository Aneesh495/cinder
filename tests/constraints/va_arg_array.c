#include <stdarg.h>
int invalid(int last,...) { va_list list; va_start(list,last); va_arg(list,int[2]); va_end(list); return 0; }
