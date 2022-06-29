struct P { int x; int y; }; struct Q { struct P p; int z; }; int main(void) { struct P p={3,4}; struct Q q={p,5}; return q.p.x+q.p.y+q.z; }
