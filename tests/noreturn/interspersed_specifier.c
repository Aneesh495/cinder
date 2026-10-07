void _Noreturn spin(void) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
