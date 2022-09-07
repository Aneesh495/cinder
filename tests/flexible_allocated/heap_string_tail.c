#include <stdlib.h>
#include <string.h>
struct S{size_t length;char data[];};int main(void){const char *s="cinder flexible storage";size_t n=strlen(s);struct S *p=malloc(sizeof(*p)+n+1);if(!p)return 1;p->length=n;memcpy(p->data,s,n+1);int ok=p->length==23&&!strcmp(p->data,s);free(p);return !ok;}
