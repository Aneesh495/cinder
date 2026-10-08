#include <stdlib.h>
struct S{int n;int data[];};int main(void){struct S *p=malloc(sizeof(*p)+2*sizeof(int));if(!p)return 1;p->n=2;p->data[0]=7;p->data[1]=11;struct S *q=realloc(p,sizeof(*p)+7*sizeof(int));if(!q){free(p);return 2;}p=q;for(int i=2;i<7;++i)p->data[i]=i+13;p->n=7;int x=p->data[0]+p->data[1]+p->data[6];free(p);return x!=37;}
