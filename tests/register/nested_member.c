struct I { int x; }; struct S { struct I i; int y; }; int main(void) { register struct S s={{3},7}; s.i.x++; return s.i.x!=4 || s.y!=7; }
