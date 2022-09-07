#include <stdlib.h>
struct S{int n;int *data[];};int main(void){int a=7,b=11;struct S *p=malloc(sizeof(*p)+2*sizeof(int *));if(!p)return 1;p->n=2;p->data[0]=&a;p->data[1]=&b;int x=*p->data[0]+*p->data[1];free(p);return x!=18;}
