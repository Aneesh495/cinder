struct S { int x; double y; }; int pick(struct S s) { return s.x+(int)s.y; } int main(void) { register struct S s={3,7.0}; return pick(s)!=10; }
