struct S{struct{int n;};};int main(void){const struct S s={.n=7};int *p=(int *)&s.n;*p=11;return 0;}
