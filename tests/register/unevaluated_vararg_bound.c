#include <stdarg.h>
int pick(float n,...) { va_list list;int a[sizeof(va_start(list,n),0)];return sizeof a!=16; } int main(void) { return pick(2.0f,7); }
