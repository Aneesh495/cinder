#include <stdarg.h>
float invalid(int tag,...) { va_list list; va_start(list,tag); float result = va_arg(list,float); va_end(list); return result; }
int main(void) { return (int)invalid(0,1.25f); }
