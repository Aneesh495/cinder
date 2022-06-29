struct P { int x; int y; }; struct Q { struct P p; int z; }; int main(void) { struct P a={1,2}; struct Q q={a,7,.p.x=9}; return q.p.x+q.p.y+q.z; }
