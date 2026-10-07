#include <stdarg.h>
long read(int tag,...) { va_list list; va_start(list,tag); long first = va_arg(list,long); va_end(list); va_start(list,tag); long second = va_arg(list,long); va_end(list); return first*3 + second*5; }
int main(void) { return read(0,11L) != 88; }
