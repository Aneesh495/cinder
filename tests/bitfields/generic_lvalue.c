struct S { unsigned a:3; unsigned b:5; }; int main(void){struct S s={5,27};_Generic(0,int:s.a,default:s.b)=6;return s.a!=6 || s.b!=27;}
