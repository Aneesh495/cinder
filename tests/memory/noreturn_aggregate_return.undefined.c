struct P { int x; double y; }; _Noreturn struct P f(void) { struct P p={3,4.5}; return p; } int main(void) { struct P p=f(); return p.x; }
