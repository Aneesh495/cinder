_Noreturn void spin(const int n) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
