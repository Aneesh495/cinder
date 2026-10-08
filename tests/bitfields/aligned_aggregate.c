struct S { _Alignas(16) char c; unsigned a:3; unsigned b:5; }; int main(void){struct S s={'M',6,25};return _Alignof(struct S)!=16 || sizeof(s)!=16 || s.c!='M' || s.a!=6 || s.b!=25;}
