#include <stdarg.h>
unsigned int invalid(int tag,...) { va_list list; va_start(list,tag); unsigned int result = va_arg(list,unsigned int); va_end(list); return result; }
int main(void) { return (int)invalid(0,-7); }
