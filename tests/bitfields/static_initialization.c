struct S { signed a:4; unsigned b:6; _Bool c:1; }; static struct S s={-7,53,1}; int main(void){return s.a!=-7 || s.b!=53 || s.c!=1;}
