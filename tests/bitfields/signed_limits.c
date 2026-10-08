struct S { signed a:3; signed b:5; }; int main(void){struct S s={-4,-16};s.b=15;return s.a!=-4 || s.b!=15;}
