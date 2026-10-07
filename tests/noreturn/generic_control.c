_Noreturn int spin(void) { for (;;) {} } int main(void) { return _Generic(spin(), int:3, default:4)!=3; }
