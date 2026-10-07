struct P { int a[2]; };
int main(void) { struct P a={{1,2}}, b={{3,4}}; int *p; for(int i=0;i<1;(p=(1?a:b).a,++i)) {} return *p; }
