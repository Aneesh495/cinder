struct S { unsigned a:3; };int main(void){struct S s={5};return _Generic(+(s.a++),int:0,default:1) || s.a!=5;}
