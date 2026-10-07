#include <stdarg.h>
long invalid(int tag,...) { va_list list; va_start(list,tag); long result = va_arg(list,long); va_end(list); va_end(list); return result; }
int main(void) { return (int)invalid(0,7L); }
