struct P { int a; int b; }; int main(void) { static struct P p={2,3}; static int *q=&p.b; *q=7; return p.b!=7 || p.a!=2; }
