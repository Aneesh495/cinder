struct S { unsigned a:3; signed b:5; }; int main(void){struct S a={5,-7};struct S b=a;a.b=12;return b.a!=5 || b.b!=-7 || a.b!=12;}
