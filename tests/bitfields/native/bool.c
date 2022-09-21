struct S { _Bool a:1; _Bool b:1; };
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a=!s.a;s.b=!s.b;return s;}
int main(void){struct S s={1,0};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==1&&s.b==0);}
