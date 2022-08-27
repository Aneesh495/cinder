struct S { int *p; }; int main(void) { int n=3; register struct S s={&n}; int *p=&*s.p; *p=7; return n!=7; }
