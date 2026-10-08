struct S { const unsigned a:3; unsigned b:5; }; int main(void){struct S s={6,25};s.b=9;return s.a!=6 || s.b!=9;}
