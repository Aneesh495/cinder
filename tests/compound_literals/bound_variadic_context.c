#include <stdarg.h>
int f(int n, ...) { va_list ap; int a[sizeof (int[]){(va_start(ap,n),0),(va_end(ap),0)} / sizeof(int)]={4,5}; return sizeof a != 2 * sizeof(int) || a[1] != 5; } int main(void) { return f(1,2); }
