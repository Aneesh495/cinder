struct P { int x; double y; }; double f(void) { static struct P p={2,3.5}; p.x+=1; return p.x+p.y; } int main(void) { return f()!=6.5 || f()!=7.5; }
