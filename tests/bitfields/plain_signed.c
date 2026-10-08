struct S { int a:4; }; int main(void){struct S s={-7};return s.a!=-7 || s.a>=0;}
