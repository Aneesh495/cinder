struct S { unsigned a:32; unsigned b:32; unsigned c:32; signed d:32; };
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a+=1;s.b+=2;s.c+=3;s.d-=4;return s;}
int main(void){struct S s={17,29,41,-53};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==19&&s.b==33&&s.c==47&&s.d==-61);}
