#include <stdarg.h>
struct Row { long values[3]; };
struct Row sum(long a,long b,long c,long d,long e,...) { va_list list; va_start(list,e); long first = va_arg(list,long); double second = va_arg(list,double); struct Row third = va_arg(list,struct Row); va_end(list); struct Row result = {{a+b+c+d+e,first+(long)second,third.values[2]}}; return result; }
int main(void) { struct Row input = {{19,23,29}}; struct Row result = sum(1,2,3,4,5,7L,11.0,input); return result.values[0] != 15 || result.values[1] != 18 || result.values[2] != 29; }
