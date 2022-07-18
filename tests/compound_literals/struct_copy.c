struct S { int x[3]; long y; }; int main(void) { struct S s = (struct S){.x={2,4,6},.y=8}; return s.x[0]+s.x[2]+s.y != 16; }
