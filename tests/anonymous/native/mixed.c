struct S{struct{double d;};union{long n;long other;};};
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.d+=1.5;s.n+=3;return s;}
int main(void){struct S s={.d=2.5,.n=7};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.d==5.5&&s.n==13);}
