struct P { int *p; int a[2]; }; int main(void) { static int x=3; static struct P p={&x,{4,5}}; ++*p.p; return x!=4 || p.a[1]!=5; }
