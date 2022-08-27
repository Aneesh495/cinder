#include <stdarg.h>
typedef int A[2];int f(A n,...) { va_list list;va_start(list,n);va_end(list);return 0; }
