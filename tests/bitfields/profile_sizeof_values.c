struct S { unsigned a:3; };int main(void){struct S s={5};return sizeof((0,s.a))!=4 || sizeof(s.a=7)!=4 || sizeof(++s.a)!=4 || sizeof(s.a++)!=4 || s.a!=5;}
