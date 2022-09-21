struct S { unsigned a:3; long p; signed b:5; long q; };
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a+=1;s.b-=2;s.q+=s.p;return s;}
int main(void){struct S s={2,17,-3,29};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==4&&s.b==-7&&s.p==17&&s.q==63);}
