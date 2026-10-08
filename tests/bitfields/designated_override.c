struct S { unsigned a:3; unsigned b:5; unsigned c:6; }; int main(void){struct S s={.a=2,.b=19,.a=7,.c=45};return s.a!=7 || s.b!=19 || s.c!=45;}
