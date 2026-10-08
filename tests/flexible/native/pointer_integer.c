struct S{long n;char data[];};
extern int reference(void (*callback)(struct S *),struct S *s,int a,int b,int c,int d,int e,int f,int g);
static void transform(struct S *s){s->n+=3;}
int main(void){struct S s={7};int result=reference(transform,&s,1,2,3,4,5,6,7);return result!=28||!(s.n==13);}
