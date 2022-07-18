struct S { long a; double b; }; long read(struct S s) { return s.a+(long)s.b; } int main(void) { return read((struct S){17,4.5}) != 21; }
