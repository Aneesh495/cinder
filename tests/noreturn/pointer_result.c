_Noreturn int *spin(void) { for (;;) {} } int main(void) { return sizeof(spin())!=8; }
