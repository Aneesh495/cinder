struct S { char c; const unsigned a:3; unsigned b:5; char d; }; int main(void){struct S s={'A',5,25,'Z'};s.c='B';s.d='Y';s.b=11;return s.c!='B' || s.a!=5 || s.b!=11 || s.d!='Y';}
