#include <stdarg.h>
double sum(int count, ...) { va_list list; va_start(list,count); double result = 0; for (int i = 0; i < count; ++i) result += va_arg(list,double); va_end(list); return result; }
int main(void) { return sum(11,1.25,2.5,3.75,4.0,5.25,6.5,7.75,8.0,9.25,10.5,11.75) != 70.5; }
