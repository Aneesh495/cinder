struct S { unsigned :3; unsigned a:4; unsigned :7; unsigned b:6; }; int main(void){struct S s={9,37};return s.a!=9 || s.b!=37 || sizeof(s)!=4;}
