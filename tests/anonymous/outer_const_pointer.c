struct S{struct{int n;};};static const struct S s={.n=31};_Static_assert(_Generic(&s.n,const int *:1,default:0),"promoted const pointer");int main(void){return s.n!=31;}
