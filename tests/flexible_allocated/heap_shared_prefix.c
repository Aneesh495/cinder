#include <stdlib.h>
struct S{int n;double data[];};static double total(struct S *p){double x=0;for(int i=0;i<p->n;++i)x+=p->data[i];return x;}int main(void){struct S *p=malloc(sizeof(*p)+3*sizeof(double));if(!p)return 1;p->n=3;p->data[0]=1.25;p->data[1]=2.5;p->data[2]=5;double x=total(p);free(p);return x!=8.75;}
