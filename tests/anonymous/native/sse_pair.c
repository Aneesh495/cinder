struct S{struct{double a,b;};};
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a+=1.5;s.b+=2.5;return s;}
int main(void){struct S s={.a=2.5,.b=3.5};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==5.5&&s.b==8.5);}
