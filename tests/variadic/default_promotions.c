#include <stdarg.h>
double sum(int tag, ...) { va_list list; va_start(list,tag); int first = va_arg(list,int); int second = va_arg(list,int); double third = va_arg(list,double); va_end(list); return first + second*3 + third*5; }
int main(void) { signed char first = -7; unsigned short second = 11; float third = 1.25f; return sum(0,first,second,third) != 32.25; }
