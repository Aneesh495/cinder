struct S { signed a:1; unsigned b:1; }; int main(void){struct S s={-1,1};s.a=0;return s.a!=0 || s.b!=1;}
