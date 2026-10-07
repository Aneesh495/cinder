#include <stdarg.h>
int read(int tag,...) { va_list list; va_start(list,tag); void *first = va_arg(list,void *); char *second = va_arg(list,char *); va_end(list); return *(char *)first + *second; }
int main(void) { char value = 7; void *pointer = &value; return read(0,&value,pointer) != 14; }
