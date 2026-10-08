struct S { unsigned :2; unsigned a:4; unsigned b:5; unsigned :0; unsigned c:3; }; int main(void){struct S s={.b=21};return s.a!=0 || s.b!=21 || s.c!=0;}
