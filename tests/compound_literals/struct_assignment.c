struct S { int a,b; }; int main(void) { struct S s; s=(struct S){3,7}; return s.a+s.b != 10; }
