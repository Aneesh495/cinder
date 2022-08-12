static _Noreturn void spin(void); static void spin(void) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
