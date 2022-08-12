_Noreturn _Noreturn void spin(void) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
