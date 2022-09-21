struct S { unsigned a:3; unsigned :0; unsigned b:4; }; int main(void){struct S s={5,12};return s.a!=5 || s.b!=12 || sizeof(s)!=8;}
