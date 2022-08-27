struct S { int x; double y; }; struct S pick(void) { register struct S s={3,7.0}; return s; } int main(void) { struct S s=pick(); return s.x!=3 || s.y!=7.0; }
