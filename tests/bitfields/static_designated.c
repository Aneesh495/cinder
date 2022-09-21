struct S { unsigned :3; unsigned a:4; unsigned b:9; }; static struct S s={.b=377,.a=11}; int main(void){return s.a!=11 || s.b!=377;}
