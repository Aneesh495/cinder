_Noreturn void spin(void) { for (;;) {} return; } int main(void) { return sizeof(&spin)!=8; }
