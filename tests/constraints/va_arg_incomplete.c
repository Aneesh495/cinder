#include <stdarg.h>
struct Incomplete;
int invalid(int last,...) { va_list list; va_start(list,last); va_arg(list,struct Incomplete); va_end(list); return 0; }
