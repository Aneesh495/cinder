struct P { int a[3]; }; int main(void) { struct P p[1]={{{1,2,3}}}; int v[sizeof p->a / sizeof(int)]={4,5,6}; return sizeof v != 3 * sizeof(int) || v[2]!=6; }
