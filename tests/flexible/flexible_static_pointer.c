struct S{int n;char data[];};static struct S s={61};static char *p=s.data;int main(void){return p!=s.data;}
