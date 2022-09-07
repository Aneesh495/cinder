struct S{long a,b,c;char data[];};
extern int reference(void (*callback)(struct S *),struct S *s,int a,int b,int c,int d,int e,int f,int g);
static void transform(struct S *s){s->a+=3;s->b+=5;s->c+=7;}
int main(void){struct S s={7,11,13};int result=reference(transform,&s,1,2,3,4,5,6,7);return result!=28||!(s.a==13&&s.b==21&&s.c==27);}
