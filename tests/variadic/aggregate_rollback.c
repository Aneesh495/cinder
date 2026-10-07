#include <stdarg.h>
struct Pair { long first; long second; };
long sum(long a,long b,long c,long d,long e,...) { va_list list; va_start(list,e); struct Pair pair = va_arg(list,struct Pair); long last = va_arg(list,long); va_end(list); return a+b+c+d+e + pair.first*3 + pair.second*5 + last*7; }
int main(void) { struct Pair pair = {7,11}; return sum(1,2,3,4,5,pair,13L) != 182; }
