#include <stdarg.h>
long invalid(int tag,...) { va_list list; va_start(list,tag); long first = va_arg(list,long); long second = va_arg(list,long); va_end(list); return first+second; }
int main(void) { return (int)invalid(0,7L); }
