struct S { unsigned :2; unsigned a:4; unsigned :3; unsigned b:5; unsigned c:3; }; int main(void){struct S s={.a=9,17,5};return s.a!=9 || s.b!=17 || s.c!=5;}
