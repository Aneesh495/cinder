#include <stdarg.h>
struct S { int x; double y; };int pick(struct S s,...) { va_list list;va_start(list,s);int v=va_arg(list,int);va_end(list);return s.x+(int)s.y+v; } int main(void) { return pick((struct S){2,3.0},7)!=12; }
