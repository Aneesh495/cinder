#include <stdlib.h>
struct S{int n;short data[][3];};int main(void){struct S *p=malloc(sizeof(*p)+4*sizeof(short[3]));if(!p)return 1;p->n=4;for(int i=0;i<4;++i)for(int j=0;j<3;++j)p->data[i][j]=(short)(10*i+j);int x=p->data[3][2];free(p);return x!=32;}
