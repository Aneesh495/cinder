struct S { unsigned a:27; signed b:10; unsigned c:22; };
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a+=1;s.b-=2;s.c+=3;return s;}
int main(void){struct S s={1234567,-17,3333333};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==1234569&&s.b==-21&&s.c==3333339);}
