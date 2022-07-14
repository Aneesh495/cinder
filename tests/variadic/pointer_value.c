#include <stdarg.h>
int *select(int tag, ...) { va_list list; va_start(list,tag); int *result = va_arg(list,int *); va_end(list); return result; }
int main(void) { int value = 17; int *pointer = select(0,&value); return pointer != &value || *pointer != 17; }
