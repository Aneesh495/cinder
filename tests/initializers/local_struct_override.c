struct P { int x; int y; }; struct Q { struct P p; int z; }; int main(void) { struct Q q = {.p={1,2}, .z=4, .p={5}}; return q.p.x+q.p.y+q.z; }
