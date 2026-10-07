struct P { const int x; int a[2]; }; int main(void) { const struct P p={.a[1]=23}; return p.x+p.a[0]+p.a[1]; }
