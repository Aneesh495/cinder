struct S { int a[4]; int tail; }; int main(void) { struct S s; for (int i = 0; i < 4; ++i) s.a[i] = i * 6; s.tail = 9; return s.a[2] + s.a[3] + (s.tail - 3); }
