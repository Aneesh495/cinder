struct S { unsigned a:31; }; int main(void){struct S s={2147483647U};return _Generic(+s.a,int:0,default:1) || s.a-2147483647!=0;}
