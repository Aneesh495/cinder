struct S { unsigned n:3; }; int main(void){struct S s={1};return sizeof(_Generic(0,int:s.n));}
