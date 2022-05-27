struct S { int x; long y; }; int main(void) { struct S s; struct S *p = &s; p->x = 13; p->y = 31; return p->x + p->y; }
