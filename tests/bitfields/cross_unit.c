struct S { unsigned a:27; unsigned b:10; unsigned c:22; }; int main(void){struct S s={1234567,777,3333333};return s.a!=1234567 || s.b!=777 || s.c!=3333333 || sizeof(s)!=8;}
