struct S { int n; };int main(void) { register struct S s={3};return &s!=0; }
