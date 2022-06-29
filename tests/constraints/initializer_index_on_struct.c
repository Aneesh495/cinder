struct P { int x; }; int main(void) { struct P p={[0]=2}; return p.x; }
