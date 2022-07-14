#include <stdarg.h>
int invalid(int tag,...) { __cinder_va_start((struct __cinder_va_state *)1L,tag); return 0; }
int main(void) { return invalid(0,1); }
