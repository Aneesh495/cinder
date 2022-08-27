struct S { int a[3]; }; int main(void) { register struct S s={{3,5,7}}; return sizeof s.a!=12; }
