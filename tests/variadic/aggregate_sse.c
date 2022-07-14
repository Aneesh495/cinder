#include <stdarg.h>
struct Pair { double first; double second; };
double sum(int count, ...) { va_list list; va_start(list,count); double result = 0; for (int i = 0; i < count; ++i) { struct Pair input = va_arg(list,struct Pair); result += input.first + input.second*3; } va_end(list); return result; }
int main(void) { struct Pair a = {1,2}, b = {3,4}, c = {5,6}, d = {7,8}, e = {9,10}; return sum(5,a,b,c,d,e) != 115; }
