_Noreturn float spin(void) { for (;;) {} } int main(void) { return sizeof(spin())!=4; }
