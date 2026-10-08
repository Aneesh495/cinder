struct S{const struct{int n;};};int main(void){struct S s={.n=7};s.n=11;return 0;}
