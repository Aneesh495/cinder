struct S { long x; double y; }; int main(void) { return (struct S){.y=2.5,.x=9}.x != 9; }
