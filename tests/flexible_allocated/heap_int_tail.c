#include <stdlib.h>
struct S{int n;int data[];};int main(void){int count=17;struct S *p=malloc(sizeof(*p)+(size_t)count*sizeof(int));if(!p)return 1;p->n=count;for(int i=0;i<count;++i)p->data[i]=i*i;int sum=0;for(int i=0;i<p->n;++i)sum+=p->data[i];free(p);return sum!=1496;}
