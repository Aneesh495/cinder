typedef int I; _Noreturn I spin(void) { for (;;) {} } int main(void) { return sizeof(spin())!=4; }
