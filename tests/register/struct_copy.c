struct S { int x; double y; }; int main(void) { register struct S a={3,7.0}; struct S b=a; register struct S c=b; return c.x!=3 || c.y!=7.0; }
