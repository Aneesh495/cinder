#include <stdarg.h>
long invalid(int tag,...) { va_list first,second; va_start(first,tag); va_end(first); va_copy(second,first); long result = va_arg(second,long); va_end(second); return result; }
int main(void) { return (int)invalid(0,7L); }
