#include <stdarg.h>
int pick(int a[],...); int pick(int *a,...) { va_list list;va_start(list,a);int n=va_arg(list,int);va_end(list);return *a+n; } int main(void) { int n=3;return pick(&n,7)!=10; }
