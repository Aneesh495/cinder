#include <stdarg.h>
int main(void) { va_list ap; int x; int a[sizeof (int){(va_start(ap,x),0)}]; return 0; }
