struct S{long n; double weight;char data[];};
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.n+=3;s.weight+=1.25;return s;}
int main(void){struct S s={7,2.5};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.n==13&&s.weight==5.0);}
