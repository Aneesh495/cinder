#include <stdarg.h>
long next(va_list *list) { return va_arg(*list,long); }
long sum(int tag, ...) { va_list list; va_start(list,tag); long first = next(&list); long second = next(&list); long third = va_arg(list,long); va_end(list); return first + second*3 + third*5; }
int main(void) { return sum(0,3L,7L,11L) != 79; }
