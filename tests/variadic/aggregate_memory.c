#include <stdarg.h>
struct Row { long values[3]; };
long sum(int count, ...) { va_list list; va_start(list,count); long result = 0; for (int i = 0; i < count; ++i) { struct Row input = va_arg(list,struct Row); result += input.values[0] + input.values[1]*3 + input.values[2]*5; } va_end(list); return result; }
int main(void) { struct Row a = {{1,2,3}}, b = {{4,5,6}}; return sum(2,a,b) != 71; }
