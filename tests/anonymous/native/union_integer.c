union S{struct{long n;};double d;};
extern union S reference(union S (*callback)(union S),union S s,int a,int b,int c,int d,int e,int f,int g);
static union S transform(union S s){s.n+=3;return s;}
int main(void){union S s={.n=7};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.n==13);}
