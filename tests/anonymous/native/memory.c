struct S{struct{long a,b;};union{long c;long other;};};
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a+=3;s.b+=5;s.c+=7;return s;}
int main(void){struct S s={.a=7,.b=11,.c=13};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==13&&s.b==21&&s.c==27);}
