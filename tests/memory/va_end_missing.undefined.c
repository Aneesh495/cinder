#include <stdarg.h>
long invalid(int tag,...) { va_list list; va_start(list,tag); return va_arg(list,long); }
int main(void) { return (int)invalid(0,7L); }
