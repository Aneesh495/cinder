typedef void F(void); _Noreturn F spin; void spin(void) { for (;;) {} } int main(void) { return sizeof(&spin)!=8; }
