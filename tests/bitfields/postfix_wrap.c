struct S { unsigned a:3; unsigned b:5; }; int main(void){struct S s={7,11};int r=s.a++;return r!=7 || s.a!=0 || s.b!=11;}
