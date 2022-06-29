struct P { int x; }; int main(void) { struct P p={.x[0]=2}; return p.x; }
