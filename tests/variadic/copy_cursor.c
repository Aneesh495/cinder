#include <stdarg.h>
long sum(int tag, ...) { va_list first, second; va_start(first,tag); long one = va_arg(first,long); va_copy(second,first); long two = va_arg(first,long); long three = va_arg(first,long); long copied = va_arg(second,long); va_end(second); va_end(first); return one + two*3 + three*5 + copied*7; }
int main(void) { return sum(0,3L,7L,11L) != 128; }
