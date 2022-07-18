struct S { long x[3]; }; struct S make(long value) { return (struct S){{value,value+1,value+2}}; } int main(void) { struct S s=make(7); return s.x[0]+s.x[2] != 16; }
