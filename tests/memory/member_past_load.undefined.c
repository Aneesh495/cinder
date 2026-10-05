struct S { int x; int y; }; int main(void) { struct S s; s.x = 1; s.y = 2; int *p = &s.x; return p[1]; }
