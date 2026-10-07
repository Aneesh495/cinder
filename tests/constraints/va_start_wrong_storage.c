#include <stdarg.h>
int invalid(int last,...) { int list; va_start(list,last); return 0; }
