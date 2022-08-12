_Noreturn double spin(void) { for (;;) {} } int main(void) { return sizeof(spin())!=8; }
