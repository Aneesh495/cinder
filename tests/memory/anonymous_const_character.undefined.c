struct S{const struct{int n;};};int main(void){struct S s={.n=7};unsigned char *p=(unsigned char *)&s.n;*p=0;return 0;}
