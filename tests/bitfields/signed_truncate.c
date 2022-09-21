struct S { signed a:3; unsigned b:5; }; int main(void){struct S s={0,23};int r=(s.a=6);return r!=-2 || s.a!=-2 || s.b!=23;}
