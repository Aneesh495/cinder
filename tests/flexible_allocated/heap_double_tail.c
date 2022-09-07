#include <stdlib.h>
struct S{int n;double data[];};int main(void){struct S *p=malloc(sizeof(*p)+4*sizeof(double));if(!p)return 1;p->n=4;for(int i=0;i<4;++i)p->data[i]=0.5+i;double sum=0;for(int i=0;i<p->n;++i)sum+=p->data[i];free(p);return sum!=8.0;}
