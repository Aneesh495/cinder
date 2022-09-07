struct S{double n;int data[];};
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.n+=1.25;return s;}
int main(void){struct S s={2.5};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.n==5.0);}
