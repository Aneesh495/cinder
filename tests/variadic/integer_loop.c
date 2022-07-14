#include <stdarg.h>
long sum(int count, ...) { va_list list; va_start(list,count); long result = 0; for (int i = 0; i < count; ++i) result += va_arg(list,long); va_end(list); return result; }
int main(void) { return sum(9,1L,2L,3L,4L,5L,6L,7L,8L,9L) != 45; }
