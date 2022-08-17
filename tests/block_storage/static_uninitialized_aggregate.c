struct P { int a; double b; }; int f(void) { static struct P p; if(p.a==0) { p.a=3; p.b=5.0; } else ++p.a; return p.a+(int)p.b; } int main(void) { return f()!=8 || f()!=9; }
