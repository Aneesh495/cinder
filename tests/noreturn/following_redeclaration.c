void spin(void) { for (;;) {} } _Noreturn void spin(void); int main(void) { return sizeof(&spin)!=8; }
