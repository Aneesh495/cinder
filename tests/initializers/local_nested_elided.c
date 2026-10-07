struct Inner { int x; int y; }; struct Outer { struct Inner inner; int z; }; int main(void) { struct Outer value={2,3,5}; return value.inner.y+value.z; }
