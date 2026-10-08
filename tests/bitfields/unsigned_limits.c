struct S { unsigned a:3; unsigned b:5; }; int main(void){struct S s={7,31};return s.a!=7 || s.b!=31 || sizeof(s)!=4;}
