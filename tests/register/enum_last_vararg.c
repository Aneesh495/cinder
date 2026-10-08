#include <stdarg.h>
enum E { A=3,B=7 };int pick(enum E n,...) { va_list list;va_start(list,n);int v=va_arg(list,int);va_end(list);return n+v; } int main(void) { return pick(A,7)!=10; }
