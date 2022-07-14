#include <stdarg.h>
struct Pair { double fraction; long whole; };
double sum(int count, ...) { va_list list; va_start(list,count); double result = 0; for (int i = 0; i < count; ++i) { struct Pair input = va_arg(list,struct Pair); result += input.whole + input.fraction*3; } va_end(list); return result; }
int main(void) { struct Pair a = {1,2}, b = {3,4}, c = {5,6}, d = {7,8}, e = {9,10}, f = {11,12}; return sum(6,a,b,c,d,e,f) != 150; }
