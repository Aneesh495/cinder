union P { int x; long y; }; int main(void) { union P p={1,2}; return p.x; }
