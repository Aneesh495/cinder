struct S { int x; double y; }; int main(void) { register struct S s={3,7.0}; s.x+=2; s.y*=2.0; return s.x!=5 || s.y!=14.0; }
