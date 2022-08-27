#include <stdarg.h>
int pick(float n,...) { va_list list;return _Generic(1,int:3,default:(va_start(list,n),7)); } int main(void) { return pick(2.0f,7)!=3; }
