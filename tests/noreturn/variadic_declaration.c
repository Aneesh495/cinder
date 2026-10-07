_Noreturn void spin(int n, ...) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
