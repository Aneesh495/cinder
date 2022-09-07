#include <stdlib.h>
struct S{long n;int data[];};int main(void){struct S *p=calloc(1,sizeof(*p)+9*sizeof(int));if(!p)return 1;int sum=0;for(int i=0;i<9;++i)sum+=p->data[i];p->data[8]=71;int x=p->data[8];free(p);return sum!=0||x!=71;}
