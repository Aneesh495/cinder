_Noreturn void spin(void) { for (;;) {} } int present(void (*p)(void)) { return p!=0; } int main(void) { return present(spin)!=1; }
