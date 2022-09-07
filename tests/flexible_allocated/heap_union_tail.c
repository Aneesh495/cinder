#include <stdlib.h>
struct S{int n;int data[];};union U{struct S s;long fixed;};int main(void){union U *p=malloc(sizeof(*p)+5*sizeof(int));if(!p)return 1;p->s.n=5;for(int i=0;i<5;++i)p->s.data[i]=i+3;int x=p->s.data[4];free(p);return x!=7;}
