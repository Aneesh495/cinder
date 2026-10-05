struct A { int x; }; struct B { char tag; struct A inner; }; int main(void) { struct B b; b.tag = 9; b.inner.x = 42; return b.tag + b.inner.x; }
