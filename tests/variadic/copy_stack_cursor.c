#include <stdarg.h>
long sum(long a,long b,long c,long d,long e,long f,...) { va_list first,second; va_start(first,f); long one = va_arg(first,long); va_copy(second,first); long two = va_arg(first,long); long copied = va_arg(second,long); va_end(second); va_end(first); return a+b+c+d+e+f + one*3 + two*5 + copied*7; }
int main(void) { return sum(1,2,3,4,5,6,7L,11L) != 174; }
