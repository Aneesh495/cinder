struct P { int x; double y; }; struct P f(int n) { struct P p={3,4.0}; if(n) goto done; p.x=9; done: return p; } int main(void) { struct P a=f(1),b=f(0); return a.x!=3 || a.y!=4.0 || b.x!=9; }
