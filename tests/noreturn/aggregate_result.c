struct P { int x; double y; }; _Noreturn struct P spin(void) { for (;;) {} } int main(void) { return sizeof(spin())!=16; }
