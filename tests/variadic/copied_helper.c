#include <stdarg.h>
long inspect(va_list input) { va_list copy; va_copy(copy,input); long value = va_arg(copy,long); va_end(copy); return value; }
long sum(int tag, ...) { va_list list; va_start(list,tag); long first = inspect(list); long second = va_arg(list,long); va_end(list); return first*3 + second*5; }
int main(void) { return sum(0,11L) != 88; }
