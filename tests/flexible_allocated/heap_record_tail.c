#include <stdlib.h>
struct E{int n;double weight;};struct S{int count;struct E data[];};int main(void){struct S *p=malloc(sizeof(*p)+3*sizeof(struct E));if(!p)return 1;p->count=3;for(int i=0;i<3;++i){p->data[i].n=i+7;p->data[i].weight=i+0.25;}int n=p->data[2].n;double d=p->data[0].weight+p->data[1].weight+p->data[2].weight;free(p);return n!=9||d!=3.75;}
