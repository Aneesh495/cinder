#include <stdarg.h>
void finish(va_list list) { va_end(list); }
int invalid(int tag,...) { va_list list; va_start(list,tag); finish(list); return 0; }
int main(void) { return invalid(0,1); }
