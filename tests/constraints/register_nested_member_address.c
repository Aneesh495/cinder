struct I { int n; };struct S { struct I i; };int main(void) { register struct S s={{3}};return &s.i.n!=0; }
