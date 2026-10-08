struct S{struct{int a[3];};};static struct S s={.a={47,53,59}};static int *p=&s.a[1];int main(void){return *p!=53||p!=s.a+1;}
