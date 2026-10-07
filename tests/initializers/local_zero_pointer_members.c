struct P { int *p; int (*f)(int); int x; }; int main(void) { struct P p={.x=11}; return p.x+(p.p==0)+(p.f==0); }
