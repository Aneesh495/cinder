struct S { int a[2]; };int main(void) { register struct S s={{3,7}};return s.a[0]; }
