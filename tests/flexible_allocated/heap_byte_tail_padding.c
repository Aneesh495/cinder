#include <stdlib.h>
#include <stddef.h>
struct S{long n;char tag;char data[];};int main(void){size_t total=sizeof(struct S)+13;struct S *p=malloc(total);if(!p)return 1;size_t count=total - offsetof(struct S,data);p->n=(long)count;p->tag=2;for(size_t i=0;i<count;++i)p->data[i]=(char)(i+1);int x=p->data[count-1];free(p);return count!=20||x!=20;}
