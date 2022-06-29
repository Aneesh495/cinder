struct P { int x; int y; }; struct Q { struct P p; int z; }; struct R { struct Q q; int w; }; int main(void) { struct Q a={{1,2},3}; struct R r={a,4,.q.p={8}}; return r.q.p.x+r.q.p.y+r.q.z+r.w; }
