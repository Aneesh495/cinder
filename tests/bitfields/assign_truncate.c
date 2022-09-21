struct S { unsigned a:3; unsigned b:5; }; int main(void){struct S s={0,19};unsigned r=(s.a=14);return r!=6 || s.a!=6 || s.b!=19;}
