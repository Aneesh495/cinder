#include <stdlib.h>
struct S{int n;int data[];};int main(void){struct S *a=malloc(sizeof(*a)+3*sizeof(int)),*b=malloc(sizeof(*b)+3*sizeof(int));if(!a||!b){free(a);free(b);return 1;}a->n=7;b->n=11;for(int i=0;i<3;++i){a->data[i]=20+i;b->data[i]=30+i;}*b=*a;int ok=b->n==7&&b->data[0]==30&&b->data[2]==32;free(a);free(b);return !ok;}
