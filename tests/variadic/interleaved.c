#include <stdarg.h>
double sum(int tag, ...) { va_list list; va_start(list,tag); double result = 0; for (int i = 0; i < 10; ++i) { result += va_arg(list,long); result += va_arg(list,double); } va_end(list); return result; }
int main(void) { return sum(0,1L,1.5,2L,2.5,3L,3.5,4L,4.5,5L,5.5,6L,6.5,7L,7.5,8L,8.5,9L,9.5,10L,10.5) != 115; }
