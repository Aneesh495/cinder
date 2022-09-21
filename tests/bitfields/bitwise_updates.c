struct S { unsigned a:6; unsigned b:7; }; int main(void){struct S s={45,99};s.a&=23;s.b^=17;s.a|=32;s.b>>=1;return s.a!=37 || s.b!=57;}
