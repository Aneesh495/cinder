struct S { unsigned a:32; }; int main(void){struct S s={4294967295U};return s.a!=4294967295U || _Generic(+s.a,unsigned int:0,default:1);}
