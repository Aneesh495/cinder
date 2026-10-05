struct S { char x; int y; }; void set(int *p) { *p = 40; } int main(void) { struct S s; set(&s.y); return s.y; }
