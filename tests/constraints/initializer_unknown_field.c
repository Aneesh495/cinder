struct P { int x; }; int main(void) { struct P p={.missing=2}; return p.x; }
