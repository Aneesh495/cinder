_Noreturn void spin(void) { do {} while (1); } int main(void) { return sizeof(&spin)!=8; }
