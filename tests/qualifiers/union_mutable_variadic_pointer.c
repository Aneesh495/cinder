#include <stdarg.h>
union U{const int fixed;int live;};void write(int n,...){va_list ap;va_start(ap,n);int*p=va_arg(ap,int*);*p=11;va_end(ap);}int main(void){union U u={.live=3};write(1,&u.live);return u.live!=11;}
