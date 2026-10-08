struct S{const struct{int n;};};int main(void){struct S s={.n=7};int *p=(int *)&s.n;*p=11;return 0;}
