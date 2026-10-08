union S { unsigned a:7; int n; };
extern union S reference(union S (*callback)(union S),union S s,int a,int b,int c,int d,int e,int f,int g);
static union S transform(union S s){s.a+=3;return s;}
int main(void){union S s={35};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==41);}
