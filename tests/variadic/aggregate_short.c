#include <stdarg.h>
struct Bytes { unsigned char value[3]; };
int sum(int count, ...) { va_list list; va_start(list,count); int result = 0; for (int i = 0; i < count; ++i) { struct Bytes input = va_arg(list,struct Bytes); result += input.value[0] + input.value[1]*3 + input.value[2]*5; } va_end(list); return result; }
int main(void) { struct Bytes a = {{1,2,3}}, b = {{4,5,6}}; return sum(2,a,b) != 71; }
