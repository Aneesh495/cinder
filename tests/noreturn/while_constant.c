_Noreturn void spin(void) { while (1) {} } int main(void) { return sizeof(&spin)!=8; }
