struct P { int x; double y; }; struct P f(void) { static struct P p={2,3.5}; ++p.x; return p; } int main(void) { struct P a=f(),b=f(); return a.x!=3 || b.x!=4 || a.y!=3.5; }
