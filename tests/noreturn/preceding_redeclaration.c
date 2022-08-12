_Noreturn void spin(void); void spin(void) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
