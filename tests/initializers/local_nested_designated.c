struct Inner { int x; int y; }; struct Outer { struct Inner inner; int z; }; int main(void) { struct Outer value={.inner.y=59,61}; return value.inner.x+value.inner.y+value.z; }
