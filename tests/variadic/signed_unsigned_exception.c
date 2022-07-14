#include <stdarg.h>
long sum(int tag,...) { va_list list; va_start(list,tag); unsigned long first = va_arg(list,unsigned long); long second = va_arg(list,long); va_end(list); return (long)first+second; }
int main(void) { return sum(0,7L,11UL) != 18; }
