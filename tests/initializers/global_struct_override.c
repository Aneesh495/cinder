struct P { int x; int y; }; struct Q { struct P p; int z; }; struct Q q = {.p={1,2}, .z=4, .p={5}}; int main(void) { return q.p.x+q.p.y+q.z; }
