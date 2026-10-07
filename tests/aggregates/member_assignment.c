struct P { int x; int y; }; struct Q { struct P p; int z; };
int main(void) { struct Q a={{1,2},9}, b={{3,4},8}; a.p=b.p; return a.p.x+a.p.y+a.z; }
